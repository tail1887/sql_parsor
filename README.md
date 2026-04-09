# 제출용 합본 (A조 + B조)

이 브랜치(`submission/combined`)는 **동일 목표(SQL 처리기)** 를 바라본 두 구현을 한 저장소에서 제출하기 위한 레이아웃입니다.

| 디렉터리 | 설명 |
|----------|------|
| **`team-a/`** | A조: C 기반 미니 SQL 엔진 (`INSERT` / `SELECT`), CSV 저장, 선택적 웹 데모(`demo/`). 상세는 `team-a/README.md`. |
| **`team-b/`** | B조: 출입 관리 시나리오 중심 구현(STUDENT·ENTRY_LOG 등), CSV + 바이너리 로그. 상세는 `team-b/README.md`. |

**일상 개발·`master` 브랜치**는 A조 프로젝트 루트 구조를 유지합니다. 합본은 이 브랜치에서만 유지합니다.

## 제출 링크

- **브랜치 전체:** https://github.com/tail1887/sql_parsor/tree/submission/combined  
- (ZIP 다운로드: GitHub 해당 브랜치 페이지에서 **Code → Download ZIP**)

## 빌드 요약

- **A조:** `team-a/`에서 CMake 빌드. 웹 데모는 `team-a/demo`의 `README.md` 또는 상위 `team-a/README.md` 참고.
- **B조:** `team-b/README.md` 및 `team-b/Makefile` 참고.
