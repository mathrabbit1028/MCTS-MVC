# PUCT Strategy Notes (MVC)

이 문서는 `treePolicy::setEstimatePolicy(...)`로 주입하는 PUCT prior(정점 포함 확률) 설계 메모입니다.

핵심 포인트:

> LP의 `x_v`는 확률 자체가 아니라 힌트다.
> 실제 PUCT prior로 쓰려면 샘플링/보정으로 분포로 바꿔야 한다.

---

## 후보 전략 요약

1. **LP 그대로**: `p_v = x_v` (baseline, 빠름)
2. **piecewise/threshold 보정**: half-integral 구조 반영
3. **randomized rounding**: `x`로 샘플링 + uncovered edge 보정
4. **perturbation ensemble**: 목적함수 섭동 LP 여러 번 + rounding 빈도
5. **multiple optimal LP sampling**: 여러 최적 extreme point 평균
6. **Gibbs / MCMC**: `P(S) ∝ exp(-β|S|)`에서 포함 빈도 추정
7. **hybrid**: LP + degree/core 구조 특성 보정
8. **dual-based**: `p_v ∝ Σ_{e∋v} y_e`

---

## 현재 코드 반영 상태 (이 브랜치)

- `src/test/perf_mcts.cpp`
   - 현재 활성 추정기: **perturbation-LP 기반** (전략 4 계열)
   - active core에서 여러 trial로 근사 LP 해를 구하고 `x_v > 0.5` 빈도를 prior로 사용

- `src/test/test_estimator.cpp`
   - 현재 활성 추정기: **dual-based** (전략 8)
   - crown decomposition 후 core 정점만 평가
   - `p_v`와 brute-force MVC 포함 빈도(`mvc_inclusion_count`)를 함께 출력해 prior 품질 비교

---

## 구현 인터페이스

추정기 함수 시그니처:

`double estimatePolicy(const State& state, const Graph& graph, bool include)`

- 입력
   - `state.actionVertex`: 현재 분기 정점
   - `state.possibleVertices`: 현재 active core
   - `include`: include/exclude 분기 여부
- 출력
   - include 분기 prior 확률 `p`
   - 호출부에서 `include ? p : (1-p)` 사용

---

## 실험 팁

- crown decomposition을 먼저 적용한 코어에서 prior를 계산하면 노이즈가 줄어듭니다.
- `p`를 `[0.01, 0.99]`로 클리핑하면 탐색 완전 고착을 완화할 수 있습니다.
- 성능 측정은 `perf_mcts.cpp`의 CSV(`result/`)와 `test_estimator.cpp`의 포함 빈도 비교를 함께 보는 것이 좋습니다.
