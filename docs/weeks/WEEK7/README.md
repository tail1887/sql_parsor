# WEEK7 — B+ 트리 인덱스 (연계 과제)

**수요 코딩회·연계 프로젝트** 범위의 문서입니다.  
동작·API의 **정본은 여전히** [`../../01-product-planning.md`](../../01-product-planning.md) ~ [`../../04-development-guide.md`](../../04-development-guide.md) 이며, B+ 트리 도입 후 바뀌는 계약은 그때그때 정본에 반영합니다.

| 파일 | 설명 |
| --- | --- |
| [`assignment.md`](assignment.md) | 과제 요구·수용 기준·벤치·발표 체크리스트 |
| [`learning-guide.md`](learning-guide.md) | 학습 순서·핵심 개념·자기 점검 |
| [`sequences.md`](sequences.md) | **공부용** 실행 시퀀스(MVP와 분리된 WEEK7 Mermaid) |
| [`implementation-order.md`](implementation-order.md) | **확정** 개발 순서(단계 0~7, 단계별 완료 기준) |
| [`presentation-script.md`](presentation-script.md) | **학습·발표용** 상세 노트 — **§2.4에 Mermaid·ASCII 다이어그램 전부 포함**, 이론·경로·벤치·FAQ |
| [`presentation-script-easy.md`](presentation-script-easy.md) | **난해한 점 풀이** — 줄이는 요약이 아니라, 실행 흐름 순으로 개념 전개 (정본은 `03-api-reference`) |
| [`presentation-script-full.md`](presentation-script-full.md) | **통합본** — `presentation-script` + 각 절 직후 **(통합·풀이)** (`-easy` 내용 삽입) |
| [`presentation-visuals.md`](presentation-visuals.md) | **다이어그램** — 전형적 B+ 개념도 ↔ 리프 payload, 검색·INSERT·벤치(Mermaid·ASCII) |

구현·문서는 위 파일들을 기준으로 유지하고, 계약 변경 시 정본(`docs/02`, `03`)과 루트 [`README.md`](../../../README.md)를 함께 갱신합니다.
