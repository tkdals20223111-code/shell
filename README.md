# 미니 쉘

Linux 시스템 프로그래밍 팀 프로젝트로, C 언어로 작성한 미니 쉘이다. 파일과 디렉터리를 다루는 명령어와 입출력 재지향, 파이프, 백그라운드 실행을 지원한다.

## 빌드 및 실행

Ubuntu 또는 WSL의 Ubuntu에서 GCC를 사용한다. 저장소의 최상위 폴더에서 다음 명령을 실행한다.

```bash
gcc -Wall -Wextra -g src/main.c -o minishell
./minishell
```

`mini-shell>` 프롬프트가 표시되면 명령을 입력한다. 종료하려면 `exit`를 입력한다.

## 지원 기능

| 구분 | 명령 및 기능 |
|---|---|
| 경로 및 디렉터리 | `pwd`, `cd`, `ls`, `mkdir`, `rmdir` |
| 파일 관리 | `ln`, `cp`, `rm`, `mv`, `touch` |
| 파일 내용 조회 및 검색 | `cat`, `grep`, `head`, `tail` |
| 사용법 및 종료 | `help`, `help 명령어`, `exit` |
| 재지향 | `<` 입력, `>` 덮어쓰기, `>>` 이어 쓰기 |
| 파이프 및 백그라운드 | `명령 \| 명령`, `명령 &` |

`head`와 `tail`은 기본 10줄을 출력하며 `-n`으로 줄 수를 지정할 수 있다. 대화형 터미널에서 `Ctrl+C`와 `Ctrl+\`는 실행 중인 포그라운드 작업을 종료하고, `Ctrl+Z`는 작업을 정지한다.

## 실행 예제

`minishell`을 실행하기 전에 Ubuntu 터미널에서 시험용 파일을 만든다.

```bash
printf 'INFO start\nERROR disk full\nINFO done\n' > sample.log
```

미니 쉘을 실행한 뒤 다음 명령을 한 줄씩 입력한다.

```text
help head
head -n 2 sample.log
tail -n 1 sample.log
cat sample.log | grep ERROR
grep ERROR < sample.log
pwd > result.txt
grep ERROR sample.log >> result.txt
cat result.txt
exit
```

마지막 `cat result.txt`에서는 현재 작업 경로와 `ERROR disk full`이 차례로 출력된다.

## 지원 범위

파이프는 두 명령을 연결하는 한 개의 `|`를 지원한다. 파이프와 재지향을 한 명령 줄에서 함께 쓰거나 정지한 작업을 `fg`로 재개하는 기능은 지원하지 않는다.
