#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <inttypes.h>


// touch 구현
static void run_touch(void)
{
    const char *path = strtok(NULL, " \t");
    if (path == NULL || path[0] == '-' || strtok(NULL, " \t") != NULL) {
        fprintf(stderr, "사용법: touch <파일 경로>\n");
        return;
    }

    // 경로가 있으면 접근 시간과 수정 시간을 현재 시각으로 바꿈
    if (utimensat(AT_FDCWD, path, NULL, 0) == 0) {
        return;
    }
    if (errno != ENOENT) {
        perror("touch");
        return;
    }

    // 경로가 없으면 빈 파일을 만들고 기존 파일은 덮어쓰지 않음
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd == -1) {
        // 파일을 확인한 직후 다른 프로세스가 만들었으면 시간을 다시 갱신
        if (errno != EEXIST || utimensat(AT_FDCWD, path, NULL, 0) == -1) {
            perror("touch");
        }
        return;
    }

    if (close(fd) == -1) {
        perror("touch");
    }
}

static int parse_line_count(const char *number, size_t *count)
{
    // 음수나 숫자 뒤에 문자가 붙은 입력은 줄 수로 받지 않음
    if (number == NULL || number[0] == '\0' ||
        number[strspn(number, "0123456789")] != '\0') {
        return -1;
    }

    errno = 0;
    uintmax_t value = strtoumax(number, NULL, 10);
    if (errno == ERANGE || value > SIZE_MAX) {
        return -1;
    }

    *count = (size_t)value;
    return 0;
}

// head 구현
static void print_first_lines(FILE *file, size_t count)
{
    char *line = NULL;
    size_t capacity = 0;

    // 지정한 줄 수를 출력하면 나머지 내용은 읽지 않음
    for (size_t i = 0; i < count; i++) {
        ssize_t length = getline(&line, &capacity, file);
        if (length == -1) {
            if (!feof(file)) {
                perror("head: 읽기");
            }
            break;
        }
        if (fwrite(line, 1, (size_t)length, stdout) != (size_t)length) {
            perror("head: 출력");
            break;
        }
    }

    free(line);
}

struct tail_line {
    char *text;
    size_t length;
    struct tail_line *next;
};

static void free_tail_line(struct tail_line *line)
{
    free(line->text);
    free(line);
}

// tail 구현
static void print_last_lines(FILE *file, size_t count)
{
    if (count == 0) {
        return;
    }

    struct tail_line *first = NULL;
    struct tail_line *last = NULL;
    size_t stored = 0;
    int read_error = 0;

    // 새 줄을 뒤에 넣고 보관할 개수를 넘으면 가장 오래된 줄을 뺌
    for (;;) {
        char *line = NULL;
        size_t capacity = 0;
        ssize_t length = getline(&line, &capacity, file);
        if (length == -1) {
            if (!feof(file)) {
                perror("tail: 읽기");
                read_error = 1;
            }
            free(line);
            break;
        }

        struct tail_line *entry = malloc(sizeof(*entry));
        if (entry == NULL) {
            perror("tail: 메모리 할당");
            free(line);
            read_error = 1;
            break;
        }
        *entry = (struct tail_line){line, (size_t)length, NULL};

        if (last != NULL) {
            last->next = entry;
        } else {
            first = entry;
        }
        last = entry;

        if (stored == count) {
            struct tail_line *old = first;
            first = first->next;
            free_tail_line(old);
        } else {
            stored++;
        }
    }

    // 남은 줄을 읽은 순서대로 출력하면서 메모리를 해제함
    while (first != NULL) {
        struct tail_line *next = first->next;
        if (!read_error &&
            fwrite(first->text, 1, first->length, stdout) != first->length) {
            perror("tail: 출력");
            read_error = 1;
        }
        free_tail_line(first);
        first = next;
    }
}

static void run_line_command(const char *command, FILE *redirect_input)
{
    size_t count = 10;
    const char *path = strtok(NULL, " \t");

    // -n을 입력하면 다음 인자를 줄 수로 읽음
    if (path != NULL && strcmp(path, "-n") == 0) {
        if (parse_line_count(strtok(NULL, " \t"), &count) == -1) {
            fprintf(stderr, "%s: -n 뒤에 0 이상의 정수를 입력하세요.\n", command);
            return;
        }
        path = strtok(NULL, " \t");
    }

    if (path != NULL && (path[0] == '-' || strtok(NULL, " \t") != NULL)) {
        fprintf(stderr, "사용법: %s [-n 줄수] [파일 경로]\n", command);
        return;
    }

    // 파일 경로가 없으면 연결된 파일이나 파이프에서 읽음
    FILE *file = redirect_input != NULL ? redirect_input : stdin;
    if (path != NULL) {
        file = fopen(path, "r");
        if (file == NULL) {
            perror(command);
            return;
        }
    }

    if (strcmp(command, "head") == 0) {
        print_first_lines(file, count);
    } else {
        print_last_lines(file, count);
    }

    if (file == stdin) {
        // Ctrl+D 뒤에도 미니 쉘에서 다음 명령을 입력받을 수 있게 함
        clearerr(stdin);
    } else if (file != redirect_input) {
        fclose(file);
    }
}

// help 구현
static void run_help(void)
{
    static const struct {
        const char *name;
        const char *usage;
        const char *description;
    } entries[] = {
        {"pwd", "pwd", "현재 작업 디렉터리 출력"},
        {"cd", "cd [경로]", "작업 디렉터리 변경"},
        {"ls", "ls [디렉터리]", "디렉터리 항목 출력"},
        {"mkdir", "mkdir <디렉터리>", "디렉터리 생성"},
        {"rmdir", "rmdir <디렉터리>", "빈 디렉터리 삭제"},
        {"ln", "ln <원본> <새 이름>", "하드 링크 생성"},
        {"cp", "cp <원본> <대상>", "파일 복사"},
        {"rm", "rm <파일>", "파일 삭제"},
        {"mv", "mv <원본> <대상>", "파일 이동 또는 이름 변경"},
        {"cat", "cat [파일]", "파일 또는 입력 내용 출력"},
        {"grep", "grep <검색어> [파일]", "검색어가 포함된 줄 출력"},
        {"touch", "touch <파일>", "빈 파일 생성 또는 파일 시간 갱신"},
        {"head", "head [-n 줄수] [파일]", "처음부터 지정한 줄 수만큼 출력(기본 10줄)"},
        {"tail", "tail [-n 줄수] [파일]", "끝에서 지정한 줄 수만큼 출력(기본 10줄)"},
        {"help", "help [명령어]", "전체 명령 또는 지정한 명령의 사용법 출력"},
        {"exit", "exit", "미니 쉘 종료"},
    };

    const char *name = strtok(NULL, " \t");
    if (strtok(NULL, " \t") != NULL) {
        fprintf(stderr, "사용법: help [명령어]\n");
        return;
    }

    // 명령어를 지정하면 해당 명령의 사용법만 출력함
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        if (name == NULL || strcmp(name, entries[i].name) == 0) {
            printf("%s — %s\n", entries[i].usage, entries[i].description);
            if (name != NULL) {
                return;
            }
        }
    }

    if (name != NULL) {
        fprintf(stderr, "help: 지원하지 않는 명령어: %s\n", name);
        return;
    }

    puts("\n명령어 < 파일 — 파일에서 입력 읽기");
    puts("명령어 > 파일 — 출력으로 파일 내용 덮어쓰기");
    puts("명령어 >> 파일 — 출력으로 파일 내용 이어 쓰기");
    puts("명령어 | 명령어 — 앞 명령의 출력을 다음 명령의 입력으로 연결");
    puts("명령어 & — 백그라운드에서 실행");
}

static void run_command(char *input, FILE *redirect_input)
{
    // 공백과 탭을 기준으로 첫 단어를 가져옴
    char *command = strtok(input, " \t");

    // 공백이나 탭만 입력한 경우 명령 처리를 끝냄
    if (command == NULL) {
        return;
    }

    // pwd 구현
    if (strcmp(command, "pwd") == 0) {
        // 옵션이나 경로 인자를 받지 않음
        if (strtok(NULL, " \t") != NULL) {
            fprintf(stderr, "사용법: pwd\n");
            return;
        }

        // 현재 작업 디렉터리의 경로를 cwd에 저장
        char cwd[4096];

        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("pwd");
        }
        else {
            printf("%s\n", cwd);
        }

        // 경로 출력 또는 오류 처리 후 다음 명령을 받음
        return;
    }

    // cd 구현
    if (strcmp(command, "cd") == 0) {
        // 명령어 다음에 입력한 단어를 이동할 경로로 사용
        const char *path = strtok(NULL, " \t");

        if (path == NULL) {
            // 경로를 생략하면 HOME 환경 변수에서 홈 경로를 읽음
            path = getenv("HOME");
            if (path == NULL || path[0] == '\0') {
                fprintf(stderr, "cd: HOME이 설정되어 있지 않습니다.\n");
                return;
            }
        }
        else if (strtok(NULL, " \t") != NULL) {
            fprintf(stderr, "사용법: cd [경로]\n");
            return;
        }

        // 미니 쉘 자체의 작업 디렉터리를 변경
        if (chdir(path) == -1) {
            perror("cd");
        }

        return;
    }

    // ls 구현
    if (strcmp(command, "ls") == 0) {
        const char *path = strtok(NULL, " \t");

        if (path == NULL) {
            path = "."; // 경로를 생략하면 현재 디렉터리를 염
        }
        else if (path[0] == '-' || strtok(NULL, " \t") != NULL) {
            fprintf(stderr, "사용법: ls [디렉터리]\n");
            return;
        }

        DIR *dir = opendir(path);
        if (dir == NULL) {
            perror("ls");
            return;
        }

        while (1) {
            errno = 0;
            struct dirent *entry = readdir(dir);

            if (entry == NULL) {
                if (errno != 0) {
                    perror("ls");
                }
                break;
            }

            // 기본 목록에서는 점으로 시작하는 항목을 숨김
            if (entry->d_name[0] != '.') {
                puts(entry->d_name);
            }
        }

        if (closedir(dir) == -1) {
            perror("ls");
        }
        return;
    }

    // mkdir 구현
    if (strcmp(command, "mkdir") == 0) {
        const char *path = strtok(NULL, " \t");

        // 경로를 하나만 받음
        if (path == NULL || strtok(NULL, " \t") != NULL) {
            fprintf(stderr, "사용법: mkdir <디렉터리>\n");
            return;
        }

        if (mkdir(path, 0777) == -1) {
            perror("mkdir");
        }

        return;
    }

    // rmdir 구현
    if (strcmp(command, "rmdir") == 0) {
        const char *path = strtok(NULL, " \t");

        // 삭제할 디렉터리 경로를 하나만 받음
        if (path == NULL || strtok(NULL, " \t") != NULL) {
            fprintf(stderr, "사용법: rmdir <디렉터리>\n");
            return;
        }

        if (rmdir(path) == -1) {
            perror("rmdir");
        }

        return;
    }

    // ln 구현
    if (strcmp(command, "ln") == 0) {
        const char *source = strtok(NULL, " \t");
        const char *target = strtok(NULL, " \t");

        if (source == NULL || target == NULL ||
            source[0] == '-' || strtok(NULL, " \t") != NULL) {
            fprintf(stderr, "사용법: ln <원본 파일> <새 이름>\n");
            return;
        }

        if (link(source, target) == -1) {
            perror("ln");
        }

        return;
    }

    // cp 구현
    if (strcmp(command, "cp") == 0) {
        const char *source = strtok(NULL, " \t");
        const char *target = strtok(NULL, " \t");

        if (source == NULL || target == NULL ||
            source[0] == '-' || strtok(NULL, " \t") != NULL) {
            fprintf(stderr, "사용법: cp <원본 파일> <대상 파일>\n");
            return;
        }

        // 원본을 읽기용으로 염
        FILE *input_file = fopen(source, "rb");
        if (input_file == NULL) {
            perror("cp: 원본 열기");
            return;
        }

        // 새 대상 파일을 만들고, 이미 있으면 열지 않음
        FILE *output_file = fopen(target, "wbx");
        if (output_file == NULL) {
            perror("cp: 대상 열기");
            fclose(input_file);
            return;
        }

        // 읽은 바이트 수만큼 대상 파일에 씀
        unsigned char buffer[4096];
        size_t bytes_read;

        while ((bytes_read = fread(buffer, 1, sizeof(buffer), input_file)) > 0) {
            if (fwrite(buffer, 1, bytes_read, output_file) != bytes_read) {
                perror("cp: 쓰기");
                break;
            }
        }

        if (ferror(input_file)) {
            perror("cp: 읽기");
        }

        if (fclose(output_file) == EOF) {
            perror("cp: 대상 닫기");
        }
        fclose(input_file);

        return;
    }

    // rm 구현
    if (strcmp(command, "rm") == 0) {
        const char *path = strtok(NULL, " \t");

        // 옵션 없이 파일 경로 하나만 받음
        if (path == NULL || path[0] == '-' ||
            strtok(NULL, " \t") != NULL) {
            fprintf(stderr, "사용법: rm <파일 경로>\n");
            return;
        }

        if (unlink(path) == -1) {
            perror("rm");
        }

        return;
    }

    // mv 구현
    if (strcmp(command, "mv") == 0) {
        const char *source = strtok(NULL, " \t");
        const char *target = strtok(NULL, " \t");

        // 원본 경로와 이동할 경로를 하나씩 받음
        if (source == NULL || target == NULL ||
            source[0] == '-' || strtok(NULL, " \t") != NULL) {
            fprintf(stderr, "사용법: mv <원본 경로> <대상 경로>\n");
            return;
        }

        if (rename(source, target) == -1) {
            perror("mv");
        }

        return;
    }

    // cat 구현
    if (strcmp(command, "cat") == 0) {
        const char *path = strtok(NULL, " \t");

        if (path != NULL &&
            (path[0] == '-' || strtok(NULL, " \t") != NULL)) {
            fprintf(stderr, "사용법: cat [파일 경로]\n");
            return;
        }

        // 경로가 없으면 연결된 입력, 있으면 해당 파일을 읽음
        FILE *input_file = redirect_input != NULL ? redirect_input : stdin;
        if (path != NULL) {
            input_file = fopen(path, "rb");
            if (input_file == NULL) {
                perror("cat");
                return;
            }
        }

        int byte;
        while ((byte = fgetc(input_file)) != EOF) {
            if (putchar(byte) == EOF) {
                perror("cat: 출력");
                break;
            }
        }

        if (ferror(input_file)) {
            perror("cat: 읽기");
        }

        if (input_file == stdin) {
            // ctrl + d 이후에도 미니 쉘이 다시 입력을 받을 수 있게 함
            clearerr(stdin);
        }
        else if (input_file != redirect_input) {
            fclose(input_file);
        }

        return;
    }

    // grep 구현
    if (strcmp(command, "grep") == 0) {
        const char *pattern = strtok(NULL, " \t");
        const char *path = strtok(NULL, " \t");

        if (pattern == NULL || pattern[0] == '-' ||
            strtok(NULL, " \t") != NULL) {
            fprintf(stderr, "사용법: grep <검색어> [파일 경로]\n");
            return;
        }

        // 파일을 지정하지 않으면 연결된 입력에서 읽음
        FILE *input_file = redirect_input != NULL ? redirect_input : stdin;
        if (path != NULL) {
            input_file = fopen(path, "r");
            if (input_file == NULL) {
                perror("grep");
                return;
            }
        }

        // getline()이 줄을 저장할 공간을 마련하고 크기를 조정함
        char *line = NULL;
        size_t capacity = 0;

        while (getline(&line, &capacity, input_file) != -1) {
            if (strstr(line, pattern) != NULL) {
                fputs(line, stdout);
            }
        }

        if (ferror(input_file)) {
            perror("grep: 읽기");
        }

        free(line);
        if (input_file == stdin) {
            clearerr(stdin);
        }
        else if (input_file != redirect_input) {
            fclose(input_file);
        }

        return;
    }

    if (strcmp(command, "touch") == 0) {
        run_touch();
        return;
    }

    if (strcmp(command, "head") == 0 || strcmp(command, "tail") == 0) {
        run_line_command(command, redirect_input);
        return;
    }

    if (strcmp(command, "help") == 0) {
        run_help();
        return;
    }

    printf("명령어: %s\n", command);

    // 같은 입력에서 다음 단어를 하나씩 가져옴
    int arg_number = 1;
    char *arg = strtok(NULL, " \t");

    while (arg != NULL) {
        printf("인자 %d: %s\n", arg_number, arg);
        arg_number++;
        arg = strtok(NULL, " \t");
    }
}

static void reap_background(int signal_number)
{
    int saved_errno = errno;
    (void)signal_number;

    // 종료된 자식만 회수하고 실행 중인 작업은 기다리지 않음
    for (;;) {
        pid_t child = waitpid(-1, NULL, WNOHANG);
        if (child <= 0 && !(child == -1 && errno == EINTR)) {
            break;
        }
    }

    errno = saved_errno;
}

static int restore_job_signals(void);

// 백그라운드 실행
static void run_background(char *input)
{
    fflush(stdout);

    pid_t child = fork();
    if (child == -1) {
        perror("fork");
        return;
    }

    if (child == 0) {
        // 백그라운드 작업을 쉘과 다른 프로세스 그룹에 둠
        if (setpgid(0, 0) == -1) {
            perror("setpgid");
            _exit(1);
        }

        if (restore_job_signals() == -1) {
            _exit(1);
        }

        // 백그라운드 작업이 쉘의 명령 입력을 가져가지 않게 함
        FILE *empty_input = fopen("/dev/null", "r");
        if (empty_input == NULL) {
            perror("백그라운드 입력");
            _exit(1);
        }

        run_command(input, empty_input);
        fclose(empty_input);
        _exit(fflush(stdout) == EOF ? 1 : 0);
    }

    printf("[백그라운드 PID: %ld]\n", (long)child);
}

static void wait_for_child(pid_t child)
{
    // 시그널로 대기가 끊기면 다시 기다림
    while (waitpid(child, NULL, 0) == -1) {
        if (errno != EINTR) {
            perror("waitpid");
            return;
        }
    }
}

// 파이프 처리
static void run_pipeline_workers(char *left, char *right)
{
    // fd[0]은 읽기 끝, fd[1]은 쓰기 끝
    int fd[2];
    if (pipe(fd) == -1) {
        perror("pipe");
        return;
    }

    fflush(stdout);

    pid_t writer = fork();
    if (writer == -1) {
        perror("fork");
        close(fd[0]);
        close(fd[1]);
        return;
    }

    if (writer == 0) {
        // 왼쪽 명령의 표준출력을 파이프에 연결
        close(fd[0]);
        if (dup2(fd[1], STDOUT_FILENO) == -1) {
            perror("dup2");
            close(fd[1]);
            _exit(1);
        }
        close(fd[1]);

        run_command(left, NULL);
        _exit(fflush(stdout) == EOF ? 1 : 0);
    }

    // 부모가 쓰기 끝을 닫아야 오른쪽 명령이 입력 종료를 감지함
    close(fd[1]);

    pid_t reader = fork();
    if (reader == -1) {
        perror("fork");
    } else if (reader == 0) {
        // 읽기 끝을 명령어에서 사용할 수 있는 입력 스트림으로 바꿈
        FILE *pipe_input = fdopen(fd[0], "r");
        if (pipe_input == NULL) {
            perror("fdopen");
            close(fd[0]);
            _exit(1);
        }

        run_command(right, pipe_input);
        fclose(pipe_input);
        _exit(fflush(stdout) == EOF ? 1 : 0);
    }

    close(fd[0]);

    // 두 작업을 시작한 뒤 각각의 종료를 기다림
    wait_for_child(writer);
    if (reader > 0) {
        wait_for_child(reader);
    }
}

static void run_pipeline(char *left, char *right)
{
    sigset_t blocked;
    sigset_t previous;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGCHLD);

    // 파이프 자식은 waitpid()로 직접 회수하므로 SIGCHLD 처리를 잠시 미룸
    if (sigprocmask(SIG_BLOCK, &blocked, &previous) == -1) {
        perror("sigprocmask");
        return;
    }

    run_pipeline_workers(left, right);

    if (sigprocmask(SIG_SETMASK, &previous, NULL) == -1) {
        perror("sigprocmask");
    }
}

static int set_signal_action(int number, void (*handler)(int))
{
    struct sigaction action = {0};
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    return sigaction(number, &action, NULL);
}

static int restore_job_signals(void)
{
    // 터미널에서 받은 시그널은 실행 중인 작업이 기본 동작으로 처리함
    if (set_signal_action(SIGINT, SIG_DFL) == -1 ||
        set_signal_action(SIGTSTP, SIG_DFL) == -1 ||
        set_signal_action(SIGQUIT, SIG_DFL) == -1 ||
        set_signal_action(SIGCHLD, SIG_DFL) == -1) {
        perror("sigaction");
        return -1;
    }
    return 0;
}

// 포그라운드 실행
static void run_foreground(char *left, char *right, FILE *redirect_input)
{
    pid_t shell_group = getpgrp();
    if (!isatty(STDIN_FILENO) || tcgetpgrp(STDIN_FILENO) != shell_group) {
        if (right != NULL) {
            run_pipeline(left, right);
        } else {
            run_command(left, redirect_input);
        }
        return;
    }

    // 부모가 포그라운드 작업의 종료나 정지 상태를 직접 확인함
    sigset_t blocked;
    sigset_t previous;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &blocked, &previous) == -1) {
        perror("sigprocmask");
        return;
    }

    fflush(stdout);
    pid_t child = fork();
    if (child == -1) {
        perror("fork");
    } else if (child == 0) {
        if (setpgid(0, 0) == -1) {
            perror("setpgid");
            _exit(1);
        }
        if (restore_job_signals() == -1 ||
            sigprocmask(SIG_SETMASK, &previous, NULL) == -1) {
            _exit(1);
        }

        // 터미널의 입력과 키보드 시그널을 작업 그룹으로 보냄
        if (tcsetpgrp(STDIN_FILENO, getpgrp()) == -1) {
            perror("tcsetpgrp");
            _exit(1);
        }
        if (set_signal_action(SIGTTOU, SIG_DFL) == -1) {
            perror("sigaction");
            _exit(1);
        }

        if (right != NULL) {
            run_pipeline_workers(left, right);
        } else {
            run_command(left, redirect_input);
        }
        _exit(fflush(stdout) == EOF ? 1 : 0);
    } else {
        int status;
        pid_t result;
        do {
            result = waitpid(child, &status, WUNTRACED);
        } while (result == -1 && errno == EINTR);

        if (result == -1) {
            perror("waitpid");
        }

        // 작업이 끝나거나 멈추면 터미널을 쉘로 돌려받음
        if (tcsetpgrp(STDIN_FILENO, shell_group) == -1) {
            perror("터미널 복원");
        }

        if (result > 0 && WIFSTOPPED(status)) {
            fprintf(stderr, "\n[작업 정지 PID: %ld, 시그널: %d]\n",
                    (long)child, WSTOPSIG(status));
        } else if (result > 0 && WIFSIGNALED(status)) {
            int signal_number = WTERMSIG(status);
            const char *name = signal_number == SIGINT ? "SIGINT" :
                               signal_number == SIGQUIT ? "SIGQUIT" : "기타";
            fprintf(stderr, "\n[작업 종료: %s, 시그널: %d]\n", name, signal_number);
        }
    }

    if (sigprocmask(SIG_SETMASK, &previous, NULL) == -1) {
        perror("sigprocmask");
    }
}

// ======================================================================================
// ======================================================================================

int main(void)
{
    // 쉘은 종료 키에 직접 반응하지 않고 작업이 끝나면 터미널을 되찾음
    if (isatty(STDIN_FILENO) &&
        (set_signal_action(SIGINT, SIG_IGN) == -1 ||
         set_signal_action(SIGTSTP, SIG_IGN) == -1 ||
         set_signal_action(SIGQUIT, SIG_IGN) == -1 ||
         set_signal_action(SIGTTOU, SIG_IGN) == -1)) {
        perror("sigaction");
        return 1;
    }

    struct sigaction action = {0};
    action.sa_handler = reap_background;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &action, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    // 입력 저장 공간: 1024바이트
    char input[1024];

    // 표준출력을 보관할 변수(보관 중인 출력 연결이 없음)
    int saved_stdout = -1;

    FILE *redirect_input = NULL;

    // exit 또는 입력을 종료할 때까지 반복
    while (1) {

        if (saved_stdout != -1) {
            // 파일에 보낼 출력이 버퍼에 남아 있으면 먼저 기록
            if (fflush(stdout) == EOF) {
                perror("출력 재지향");
            }

            if (dup2(saved_stdout, STDOUT_FILENO) == -1) {
                perror("표준출력 복원");
                close(saved_stdout);
                return 1;
            }

            close(saved_stdout);
            saved_stdout = -1;
            clearerr(stdout);
        }

        // 직전 명령에서 연 입력 파일을 닫음
        if (redirect_input != NULL) {
            fclose(redirect_input);
            redirect_input = NULL;
        }

        // 명령어 입력 준비
        printf("mini-shell> ");
        // 화면에 바로 출력
        fflush(stdout);

        // stdin에서 한 줄을 읽어 input에 저장
        // 입력 종료 또는 읽기 오류로 NULL 반환 시 반복 종료
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n");
            break;
        }

        // fgets가 저장한 줄바꿈을 문자열 끝 문자 '\0'로 바꿈
        // 줄바꿈이 없으면 기존 문자열 끝 위치에 '\0'을 다시 저장
        input[strcspn(input, "\n")] = '\0';

        // Enter만 입력한 경우 다음 입력 받음
        if (input[0] == '\0') {
            continue;
        }

        // exit 처리
        // strcmp: 두 문자열이 같으면 0을 반환
        if (strcmp(input, "exit") == 0) {
            break;
        }

        // &는 명령 줄의 마지막에 한 번만 허용
        char *background = strchr(input, '&');
        if (background != NULL) {
            const char *after = background + 1;
            *background = '\0';
            const char *command_start = input + strspn(input, " \t");

            if (*command_start == '\0' ||
                after[strspn(after, " \t")] != '\0' ||
                strpbrk(input, "<>|") != NULL) {
                fprintf(stderr, "사용법: 단일 명령어 &\n");
                continue;
            }

            // 쉘의 위치나 종료 상태를 바꾸는 명령은 일반 모드에서 처리
            size_t length = strcspn(command_start, " \t");
            if ((length == 2 && strncmp(command_start, "cd", 2) == 0) ||
                (length == 4 && strncmp(command_start, "exit", 4) == 0)) {
                fprintf(stderr, "cd와 exit는 백그라운드로 실행할 수 없습니다.\n");
                continue;
            }

            run_background(input);
            continue;
        }

        // 파이프 왼쪽과 오른쪽의 명령을 분리
        char *separator = strchr(input, '|');
        if (separator != NULL) {
            *separator = '\0';
            char *right = separator + 1;

            if (input[strspn(input, " \t")] == '\0' ||
                right[strspn(right, " \t")] == '\0' ||
                strpbrk(input, "<>&") != NULL ||
                strpbrk(right, "|<>&") != NULL) {
                fprintf(stderr, "사용법: 명령어 | 명령어 (파이프 한 개)\n");
                continue;
            }

            run_foreground(input, right, NULL);
            continue;
        }

        // 입력 재지향(<) 처리
        // < 왼쪽은 실행할 명령, 오른쪽은 입력 파일명
        char *input_redirect = strchr(input, '<');

        if (input_redirect != NULL) {
            if (strpbrk(input, ">|&") != NULL) {
                fprintf(stderr, "입력 재지향은 현재 단독으로 사용할 수 있습니다.\n");
                continue;
            }

            *input_redirect = '\0';
            char *filename = strtok(input_redirect + 1, " \t");

            if (input[strspn(input, " \t")] == '\0' ||
                filename == NULL ||
                strchr(filename, '<') != NULL ||
                strtok(NULL, " \t") != NULL) {
                fprintf(stderr, "사용법: 명령어 < 파일명\n");
                continue;
            }

            redirect_input = fopen(filename, "r");
            if (redirect_input == NULL) {
                perror("입력 파일 열기");
                continue;
            }
        }

        // 출력 재지향(>, >>) 처리
        // 출력 재지향 기호의 왼쪽은 명령, 오른쪽은 출력 파일명
        char *redirect = strchr(input, '>');

        if (redirect != NULL) {
            // >는 덮어쓰기, >>는 이어 쓰기로 처리함
            int append = redirect[1] == '>';
            char *output_path = redirect + (append ? 2 : 1);
            *redirect = '\0';

            char *filename = strtok(output_path, " \t");

            if (input[strspn(input, " \t")] == '\0' ||
                filename == NULL ||
                strpbrk(filename, "<>|&") != NULL ||
                strtok(NULL, " \t") != NULL) {
                fprintf(stderr, "사용법: 명령어 > 파일명 또는 명령어 >> 파일명\n");
                continue;
            }

            saved_stdout = dup(STDOUT_FILENO);
            if (saved_stdout == -1) {
                perror("표준출력 저장");
                continue;
            }

            // 파일이 없으면 만들고, O_APPEND이면 항상 파일 끝에 씀
            int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
            int output_fd = open(filename, flags, 0666);
            if (output_fd == -1) {
                perror("출력 파일 열기");
                close(saved_stdout);
                saved_stdout = -1;
                continue;
            }

            if (dup2(output_fd, STDOUT_FILENO) == -1) {
                perror("출력 재지향");
                close(output_fd);
                close(saved_stdout);
                saved_stdout = -1;
                continue;
            }

            close(output_fd);
        }

        // cd는 쉘 자체의 작업 디렉터리를 바꾸므로 부모에서 실행
        const char *command_start = input + strspn(input, " \t");
        size_t command_length = strcspn(command_start, " \t");
        if (command_length == 2 && strncmp(command_start, "cd", 2) == 0) {
            run_command(input, redirect_input);
        } else if (command_length != 0) {
            run_foreground(input, NULL, redirect_input);
        }
    }
    return 0;
}
