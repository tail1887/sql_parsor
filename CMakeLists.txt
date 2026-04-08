# CMakeLists.txt — 빌드 시스템 정의
#
# [소스 트리 vs 빌드 트리]
# - 소스 트리: CMakeLists.txt, src/, include/, tests/ 등 저장소에 두는 파일들이 있는 루트
#   (예: cmake -S . 에서 -S 가 가리키는 경로).
# - 빌드 트리: cmake -B build 로 만든 build/ 같은 폴더. CMake 캐시, 생성된 빌드 스크립트,
#   오브젝트 파일, 링크된 실행 파일(.exe) 등 “빌드 산출물”이 생기는 곳입니다.
#   소스와 분리해 두면 build/ 만 지우고 재구성하기 쉽고, .gitignore 에 build/ 를 넣기 좋습니다.

# ------------------------------------------------------------------------------
# 1. 프로젝트 및 컴파일러 환경 설정
#    빌드 시스템의 기초를 정의합니다.
# ------------------------------------------------------------------------------

# 이 빌드 파일을 처리하기 위해 필요한 최소 CMake 버전입니다.
cmake_minimum_required(VERSION 3.16)

# 프로젝트 이름을 sql_parsor로 정하고, C 언어만 사용한다고 선언합니다.
project(sql_parsor LANGUAGES C)

# C11 표준(ISO가 정한 문법·라이브러리)으로 컴파일하도록 설정합니다.
set(CMAKE_C_STANDARD 11)

# C11을 지원하지 않는 컴파일러면 빌드를 중단하라는 강제 옵션입니다.
set(CMAKE_C_STANDARD_REQUIRED ON)

# [표준 C vs 구현(컴파일러) 확장 / CMAKE_C_EXTENSIONS OFF 의도]
# - 표준 C11: 여러 컴파일러에서 재현하기 쉬운 공통 부분.
# - 구현 확장(예: GNU 확장): 컴파일러가 표준 밖으로 허용하는 편의 기능; gnu11 모드 등에서 켜지기 쉬움.
# OFF 는 “확장을 끄고 ISO C11 에 가깝게 맞춰라”에 가깝습니다. 이식성과 예측 가능한 동작을 위해 둡니다.
set(CMAKE_C_EXTENSIONS OFF)

# ------------------------------------------------------------------------------
# 2. 실행 파일 생성 및 경로 설정
#    소스를 컴파일·링크하여 실행 파일을 만듭니다.
# ------------------------------------------------------------------------------

# [add_executable] 지정한 .c 들을 컴파일·링크해 “실행 파일 타깃” 하나를 만듭니다.
# 산출물은 빌드 트리(예: build/) 아래에 생성됩니다.
add_executable(sql_processor src/main.c src/lexer.c src/parser.c src/ast.c)

# [target_include_directories] 이 타깃을 컴파일할 때만 헤더 검색 경로(-I)에 넣습니다.
# PRIVATE: 이 타깃 빌드에만 쓰이고 다른 타깃에 전파하지 않음(sql_processor 전용).
target_include_directories(sql_processor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

# CTest용 스모크 테스트 실행 파일 — tests/test_bootstrap.c 를 별도 타깃으로 빌드합니다.
add_executable(test_bootstrap tests/test_bootstrap.c)

# test_bootstrap 컴파일 시에도 동일한 include/ 를 참조합니다.
target_include_directories(test_bootstrap PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_executable(test_lexer tests/test_lexer.c src/lexer.c)
target_include_directories(test_lexer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_executable(test_parser_insert tests/test_parser_insert.c src/parser.c src/ast.c src/lexer.c)
target_include_directories(test_parser_insert PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_executable(test_parser_select tests/test_parser_select.c src/parser.c src/ast.c src/lexer.c)
target_include_directories(test_parser_select PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

# ------------------------------------------------------------------------------
# 3. 테스트 자동화 (CTest)
#
# [enable_testing / add_test / ctest 와 회귀 검증 루프]
# - enable_testing: 이 프로젝트에서 CTest 를 쓰겠다고 켭니다.
# - add_test: 테스트 하나를 이름·실행 명령으로 등록합니다. 기본적으로 종료 코드 0 = 통과.
# - ctest: 빌드 트리에서 등록된 테스트를 일괄 실행합니다 (예: ctest --test-dir build).
# 코드를 바꿀 때마다 빌드 후 ctest 를 돌리면, 깨진 동작(회귀)을 빨리 잡는 루프가 됩니다.
# (언제 돌릴지는 개발자가 직접 치거나, CI/Git 훅 등에서 호출합니다.)
# ------------------------------------------------------------------------------

enable_testing()

# cli_no_args: 인자 없이 sql_processor 를 실행했을 때의 동작을 검증합니다.
# 설계상 usage 출력 후 exit 1 이므로, CTest 기본 규칙(비제로 = 실패)과 반대입니다.
# WILL_FAIL TRUE → “비정상 종료가 나오는 것이 의도”이면 테스트를 통과로 처리합니다.
add_test(NAME cli_no_args COMMAND $<TARGET_FILE:sql_processor>)
set_tests_properties(cli_no_args PROPERTIES WILL_FAIL TRUE)

# bootstrap_smoke: test_bootstrap 을 실행합니다. 빌드·링크·CTest 배선 등
# 최소 기반(부트스트랩)이 동작하는지 확인하는 스모크 테스트입니다.
add_test(NAME bootstrap_smoke COMMAND $<TARGET_FILE:test_bootstrap>)

add_test(NAME lexer_unit COMMAND $<TARGET_FILE:test_lexer>)
add_test(NAME parser_insert_unit COMMAND $<TARGET_FILE:test_parser_insert>)
add_test(NAME parser_select_unit COMMAND $<TARGET_FILE:test_parser_select>)
