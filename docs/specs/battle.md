# Battle prototype spec

- [한국어](#한국어)
- [English](#english)

## 한국어

상태: A의 1~3단계(시뮬레이션과 정의 파일, 실행 파일, 확인과 문서) 구현됨 (2026-10-03), 나머지는 초안. A와 B 두 단계로 나눔. 정의 파일과 규칙 상수는 프로젝트 파일 하나로 합쳐졌다(2026-10-05, `abproj.md`).

### 목표

`docs/autobattler.md`의 1단계(전투 프로토타입): 항목 1(시뮬레이션 코어), 2(공간 질의), 3(군집 이동), 4(데이터 기반
정의)의 최소한. 한 라운드짜리 전투를 엔진의 기본 도형, 디버그 라인, 이펙트로 그린다. 게임이 재미있는지 보기 전에 규칙과 게임
오브젝트 구조를 굳히는 것이 목적이다.

규칙이 서로 어떻게 맞물리는지 구현 전에 다 확인하기는 어렵다. 그래서 프로토타입을 두 단계로 나눈다.
- **A**는 한 라운드가 처음부터 끝까지 도는 가장 작은 판이다. 지상 유닛 한 종류(Crawler)가 포탄 하나와 Shield 하나로 탁
  트인 전장에서 싸운다. 규칙은 "A 규칙" 절에 모두 있다.
- **B**는 나머지 전부다: "B 규칙 (전체 프로토타입)" 절의 유닛 5종, 공중, 크기 등급, 분대, 무기 여러 개와 투사체 세 종류, 범위
  피해와 아군 피해, 지형지물과 흐름장, Jump, 공간 격자.

### 범위

| A (먼저) | B (다음) | 빠짐 (나중) |
|---|---|---|
| 칸 단위 배치 그리드(칸 하나에 유닛 하나) | 분대 단위 배치 | 높이맵 지형 (지면은 평평한 y = 0) |
| 실제 높이: 포탄의 탄도 | 공중 유닛과 고도, 3D 범위 피해 | 여러 라운드, 본부 체력 |
| 유닛 1종(Crawler), 지상만, 크기 등급 없음 | 유닛 5종, 크기 등급 3개 | 업그레이드, 테크, 카드, 전문가 |
| 유닛당 무기 하나, 능력 하나(Shield) | 무기 여러 개, Jump | AI 상대 (적 배치는 스테이지 데이터에 고정) |
| 투사체 한 종류(포탄), 방어력, 단일 피해 | 총탄과 미사일, 범위 피해, 아군 피해 | |
| 가장 가까운 적 타게팅, 곧장 다가가기와 분리(모든 쌍 비교) | 지형지물, 엄폐, 흐름장, 공간 격자 | 대량 렌더링, 애니메이션 (메시 노드 하나씩) |
| 전멸 또는 60초 시간 제한으로 끝나는 한 라운드 | | 저장, undo |
| 에디터와 따로인 실행 파일 `autobattler`, 한국어와 영어 패널(게임의 한국어 표) | | 게임 UI (지금은 ImGui 패널 하나) |
| 시간 조작: 일시정지, 0.5배, 1배, 2배, 4배 (구현 뒤에 더함) | | |

### A 규칙

A는 아래 규칙만으로 끝까지 돈다. 값은 밸런스 출발점이다. 전장 크기와 구역, 라운드 시간, 중력, 다시 고르는 주기, 멈춤 비율, 최소 피해
비율은 프로젝트 파일의 `rules`가 정하는 값이고(`abproj.md`), 표는 지금의 값을 적는다.

| 항목 | A |
|---|---|
| 전장 | 2 m 칸, 32칸 × 48행. 배치 구역은 B와 같다(플레이어 행 0–13, 적 행 34–47). 지형지물 없음, 지면은 y = 0. 칸 (x, 행)의 중심은 ((x + 0.5) · 2, 0, (행 + 0.5) · 2) m: 플레이어 쪽이 −Z, 적 쪽이 +Z |
| 배치 | 칸 하나에 Crawler 하나. 양쪽 공급 1000, Crawler 비용 100(편마다 최대 10기). 적 배치는 스테이지에 고정. 플레이어 유닛이 하나 이상이어야 Start를 누를 수 있다. Retry는 같은 배치로 배치 단계에 돌아가고, Reset은 플레이어 배치를 지운다 |
| 유닛 | Crawler 한 종류, 지상만. 크기 등급과 분대가 없다 |
| 라운드 | 30 Hz 고정 틱. 한쪽이 전멸하거나 60초(1800틱)가 되면 끝난다. 시간이 다 되면 남은 가치(`비용 × 체력 / 최대 체력`의 합)가 큰 쪽이 이기고, 같으면 무승부. 같은 틱에 양쪽이 전멸해도 무승부 |
| 타게팅 | 가장 가까운 적. 0.25초(8틱)마다, 대상이 죽으면 바로 다시 고른다. 같은 거리면 슬롯 번호가 작은 쪽. 거리(타게팅과 사거리 모두)는 지면(XZ) 위 중심 사이 거리 |
| 이동 | 대상으로 곧장 다가가고, 대상이 사거리의 90% 안이면 멈춘다. 겹친 유닛끼리 서로 밀어낸다(분리, 무게는 모두 같다). 전장 밖으로 나가지 않는다. 가까운 유닛 찾기와 분리는 모든 쌍을 직접 비교한다(공간 격자 없음). 회전은 즉시: 움직일 때는 속도 방향, 멈췄을 때는 대상 쪽을 본다 |
| 무기 | 하나. 쿨다운이 끝나고 대상이 사거리 안이면 움직이는 중에도 쏜다. 쿨다운은 0에서 시작한다. 투사체는 포탄 한 종류: 중력 9.8 m/s²의 탄도로 날고, 발사각과 목표점에서 초기 속도를 구한다. 목표점은 **리드 조준**(대상의 지금 속도로 착탄 때의 위치를 예측: 비행 시간과 예측을 3번 번갈아 구한다)에 퍼짐(반지름 `spread` 원 안의 균등 난수)을 더한 지면 위의 점이다 |
| 피해 | 포탄은 점이고, 매 틱 이전 위치에서 지금 위치까지의 선분을 유닛의 세로 원기둥(반지름 × 높이)에 대해 검사한다(뚫고 지나가지 않는다). 단일 피해: 포탄은 처음 닿은 **적** 유닛 하나에 `max(피해 - 방어력, 피해 × 0.25)`를 주고 사라진다. 아군은 지나간다. 지면에 닿으면 피해 없이 사라진다. 피해는 틱 동안 더하기만 하고 죽음은 틱 끝에 정한다 |
| 능력 | Shield 하나(아래) |
| 난수 | 시드는 스테이지에 고정된다(프로젝트 파일 `stage`의 `seed`). 같은 배치는 언제나 같은 결과를 낸다 |

| Crawler (A) | 값 |
|---|---|
| 체력, 방어력 | 120, 5 |
| 속도 | 5 m/s |
| 반지름 / 높이 | 0.5 / 0.8 m |
| 무기 Lobber | 포탄, 사거리 20 m, 피해 30, 쿨다운 1.5초, 발사각 45°, 퍼짐 0.5 m |
| Shield | 반지름 1.2 m의 구, 중심은 유닛 높이의 절반. 바깥에서 경계를 넘어 들어오는 적 포탄(다른 유닛을 노린 것도: 이웃을 지킨다)의 피해를 남은 에너지만큼 흡수한다(용량 60, 방어력 전의 피해). 다 흡수하면 포탄이 사라지고, 모자라면 실드가 0이 되고 포탄은 남은 피해를 가지고 계속 날아간다(그다음 닿은 적에게 방어력 식을 적용). 3초 동안 맞지 않으면 초당 10씩 찬다. 에너지가 0이면 꺼진다 |

A의 틱 순서: 이전 위치 기록, 대상 다시 고르기, 이동과 분리, 무기(쿨다운, 발사), 포탄(적분, 실드, 유닛, 지면 순으로 가장 먼저
닿은 것), 죽음, 실드 회복, 끝 조건.

구현에서 정한 세부(1단계):
- **슬롯 순서:** 유닛은 플레이어 것이 먼저(행, 칸 순), 그다음 적(파일 순)이다. 배치가 바뀔 때마다(놓기, 지우기, Reset, Retry) 유닛 배열을
  처음부터 다시 만든다. Start는 그대로 시작할 뿐이라 Retry는 같은 배치를 돌려준다.
- **시간은 틱으로:** 쿨다운과 실드의 기다림은 틱 수(초 × 30, 올림)로 센다. 1.5초는 정확히 45틱이다. 실드는 맞은 틱도 기다림의 첫
  틱으로 센다(3초 = 90틱 뒤에 차기 시작).
- **다시 고르기:** 모든 유닛이 `틱 % 8 == 0`인 틱에 함께 다시 고른다(엇갈리게 하지 않는다. 그건 B의 일).
- **이동:** 멈출 거리까지만 움직여 넘어서지 않는다. 둘이 서로 다가가면 한 걸음 모자란 채 멈출 수 있다(사거리의 90% 안).
  분리는 모든 쌍의 밀기를 모아 틱 끝에 한 번에 적용한다(순서와 무관). 같은 자리의 두 유닛은 X축으로, 슬롯이 작은 쪽이 왼쪽으로 갈린다.
- **포탄:** 중력을 틱마다 정확하게(위치에 `−½ g dt²`, 속도에 `−g dt`) 적용하므로 계산한 지점에 떨어진다. 실드가 일부만 받은 포탄은 실드
  경계에서 남은 피해로 계속 날아가 다음에 닿는 것을 친다. 포탄 풀(4096)이 가득 차면 무기는 자리가 날 때까지 준비된 채 기다린다.
- **이벤트:** 틱마다 지우지 않는다. 화면이 읽은 뒤 `event_count`를 0으로 돌린다(한 프레임에 틱이 여럿 돌 수 있다).
- **정의 오류:** `BattleDefs`가 `error_count`와 첫 오류 문장(`first_error`)을 들고 있어 패널이 보여 줄 수 있다. 빠진 키는 블록 헤더의
  줄 번호로, 파일 전체의 오류(빠진 `supply`, 유닛이나 `place`가 하나도 없음)는 줄 번호 없이 알린다. 공급 초과는 합이 처음 넘는 `place`의
  줄로 알린다. 오류가 있는 줄은 빠진 키 오류를 더 만들지 않도록 해당 키를 "받은 것"으로 센다.

### 정의 파일

결정(2026-10-03): 유닛 정의와 스테이지는 코드가 아니라 텍스트 파일에 둔다. 형식은 줄 단위 텍스트이고, 읽는 코드도 직접 쓴다(새
서드파티 없음, "서드파티 후보" 참고). 파일은 패키지에 들어가 시작할 때 한 번 읽힌다. 값을 바꾸면 다시 패키징(`cmake --build`)해야
하지만 C 코드는 다시 컴파일되지 않는다.

변경(2026-10-05): 두 파일(`units.txt`, `stage.txt`)과 코드에 있던 규칙 상수(칸 크기, 격자, 구역, 라운드 시간, 중력, 재선택 주기,
멈춤 비율, 최소 피해 비율)를 **프로젝트 파일 하나**, `autobattler/data/default.abproj`로 합쳤다. 문법, `rules`, `unit`, `stage`의 키
표, 버전, 읽기와 오류, 테스트는 `docs/specs/abproj.md`에 있다. 읽는 함수는 `defs_read_project`(`autobattler/defs.c`)다. B에서
더할 키(`layer`, `size`, `squad`, `footprint`, `altitude`, 여러 `weapon`, `ability jump`, `prop` 등)도 거기 "나중에"에 적혀 있다.

"A 규칙"의 표와 아래 Crawler 표의 값은 `default.abproj`가 지금 담은 값이다.

### B 규칙 (전체 프로토타입)

B는 A 위에 아래 규칙을 모두 더한 모양이다. A의 Crawler는 B에서 아래 표의 근접 Crawler(분대 16)로 바뀌고, A의 포탄 무기와
Shield 값은 B의 표를 따른다.

#### 전장

| 항목 | 값 |
|---|---|
| 좌표 | 오른손, Y 위 (`engine/math.h`). 전장 중심이 원점, 플레이어 진영은 +Z, 적 진영은 -Z |
| 칸 | 2 m 정사각형. 전장은 32칸(X) × 48행(Z) = 64 m × 96 m. 행 0이 플레이어 쪽 끝 |
| 배치 구역 | 플레이어: 행 0–13, 적: 행 34–47. 가운데 20행은 배치할 수 없다 |
| 지형지물 | 칸에 맞춘 상자 (칸 범위와 높이). 부서지지 않는다. 지상 이동을 막고, 상자를 지나는 투사체를 멈춘다. 그 칸에는 배치할 수 없다 |
| 지형지물 높이 | 낮은 벽 1.5 m (엄폐: 지상 직사를 막지만 점프로 넘는다), 바위 5 m, 탑 8 m. 공중 고도(10 m)보다 낮다 |
| 지면 | y = 0. 지상 유닛은 점프 중이 아니면 y = 0에 있다. 공중 유닛은 정의의 고도에 있다 |

#### 라운드

1. **배치:** 양쪽 모두 공급 1000. 플레이어는 자기 구역에 분대를 놓고 지운다. 적 배치는 스테이지에 고정돼 있다. Start로
   전투를 시작한다.
2. **전투:** 30 Hz 고정 틱, 최대 1800틱(60초). 입력 없음.
3. **결과:** 한쪽 유닛이 모두 죽으면 다른 쪽이 이긴다(동시에 전멸하면 무승부). 시간이 다 되면 남은 가치(유닛마다
   `분대 비용 / 분대 크기 × 체력 / 최대 체력`의 합)가 큰 쪽이 이기고, 같으면 무승부다. Retry는 같은 배치로 배치 단계에
   돌아가고, Reset은 플레이어 배치를 지운다.

#### 유닛

분대는 배치 단위다: 정의의 footprint만큼 칸을 차지하고, 전투가 시작되면 그 칸 안에 분대 크기만큼 유닛을 격자로 놓는다.
그 뒤 유닛은 각자 움직이고 싸운다(대형 유지 없음). 분대는 살아 있는 유닛 수만 센다.

초기 값은 밸런스 출발점일 뿐이다.

| | Crawler | Ranger | Mortar | Wasp | Fortress |
|---|---|---|---|---|---|
| 층 | 지상 | 지상 | 지상 | 공중 (고도 10 m) | 지상 |
| 크기 등급 | 소형 | 중형 | 중형 | 소형 | 대형 |
| 분대 크기 | 16 | 4 | 2 | 6 | 1 |
| footprint (칸) | 2×2 | 2×2 | 2×2 | 2×2 | 3×3 |
| 비용 | 100 | 150 | 200 | 200 | 400 |
| 체력 | 80 | 400 | 500 | 250 | 6000 |
| 방어력 | 0 | 5 | 10 | 0 | 40 |
| 속도 (m/s) | 7 | 4 | 3 | 8 | 2 |
| 반지름 / 높이 (m) | 0.5 / 0.8 | 0.8 / 2 | 1.2 / 2 | 0.8 / 0.6 | 2.5 / 5 |
| 무기 | Claws | Rifle | Mortar | Rockets | Cannon, Flak |
| 능력 | 없음 | Jump | 없음 | 없음 | Shield |

크기 등급은 분리(밀어내기)의 무게를 정한다: 소형 1, 중형 4, 대형 20. 큰 유닛이 작은 유닛을 밀어낸다.

#### 무기

| 무기 | 투사체 | 대상 | 사거리 (m) | 피해 | 범위 (m) | 쿨다운 (s) | 그 밖 |
|---|---|---|---|---|---|---|---|
| Claws | 근접 (투사체 없음) | 지상 | 1.2 | 25 | – | 0.6 | |
| Rifle | 총탄 | 지상, 공중 | 25 | 40 | – | 1.0 | 속도 90 m/s, 퍼짐 0.3 m |
| Mortar | 포탄 | 지상 | 10–45 | 120 | 4 | 3.0 | 발사각 60°, 퍼짐 1.5 m |
| Rockets | 미사일 | 지상 | 12 | 35 | 1.5 | 0.8 | 속도 30 m/s, 선회 3 rad/s |
| Cannon | 포탄 | 지상 | 35 | 400 | 5 | 4.0 | 발사각 15°, 퍼짐 1 m |
| Flak | 미사일 | 공중 | 30 | 60 | 3 | 1.5 | 속도 50 m/s, 선회 5 rad/s |

#### 능력

- **Jump (Ranger):** 이동 방향 4 m 안에 지형지물이 있거나 적 소형 근접 유닛이 3 m 안에 오면, 이동 방향으로 10 m를
  0.8초에 걸쳐 높이 3 m의 포물선으로 뛴다. 착지점이 지형지물 칸이면 뛰지 않는다. 쿨다운 10초. 뛰는 동안 쏘지 않고 다른
  유닛과 밀어내지 않는다.
- **Shield (Fortress):** 반지름 7 m의 구. 바깥에서 경계를 넘어 들어오는 적 투사체를 없애고 그 피해만큼 에너지를 잃는다
  (용량 1500). 4초 동안 맞지 않으면 초당 100씩 찬다. 근접 공격과 아군 투사체는 통과한다. 에너지가 0이면 꺼진다.

#### 전투 규칙

- **타게팅:** 가장 가까운 적을 먼저 친다.
  - 무기마다 대상(지상, 공중)과 사거리 안의 가장 가까운 적을 고른다.
  - 유닛의 이동 대상은 무기 중 하나라도 칠 수 있는 가장 가까운 적이다(거리 제한 없음).
  - 0.25초(8틱)마다 다시 고르고, 유닛마다 틱을 나눠 분산한다. 대상이 죽으면 바로 다시 고른다.
  - 같은 거리면 슬롯 번호가 작은 유닛.
- **이동:**
  - 지상: 이동 대상이 8 m 안이고 그 사이에 지형지물이 없으면 곧장 향하고, 아니면 흐름장을 따른다. 이동 대상이 무기
    사거리의 90% 안이면 멈춘다. 이웃과 분리하고, 지형지물 칸과 전장 밖으로 나가지 않게 밀어낸다. 몸은 이동 방향으로
    돈다(무기는 360° 포탑).
  - 공중: 지형지물을 무시하고 고도를 지키며 이동 대상으로 곧장 날아간다. 공중 유닛끼리만 분리한다.
- **흐름장:** 팀마다 둘(지상 적만, 모든 적; 공중 적은 지면에 투영). 목표 칸은 적이 있는 칸이다. 8방향 다익스트라, 정수
  비용 10/14, 지형지물 칸은 지날 수 없고 대각선으로 모서리를 자르지 않는다. 10틱마다 다시 만들며, 네 장을 틱에 나눠
  만든다. 지상만 치는 유닛(Crawler, Mortar)은 "지상 적만"을 따른다.
- **발사:** 쿨다운이 끝나고 대상이 사거리 안에 있으면 쏜다. 직사(총탄)는 총구에서 대상 중심까지 지형지물이 없을 때만
  쏜다. 포탄과 미사일은 그 검사가 없다. 포탄은 A처럼 리드 조준을 한다. 총탄과 미사일은 리드가 없다: 쏠 때 대상의 위치를 겨눈다.
- **투사체:**
  - 총탄은 직선으로 날아가 사거리만큼 가면 사라진다.
  - 포탄은 중력 9.8 m/s²로 날아가고, 발사각과 목표점(리드 조준으로 예측한 대상 위치 + 시드 난수 퍼짐)에서 초기 속도를 구한다.
  - 미사일은 대상을 향해 선회하고, 대상이 죽으면 마지막 위치로 간다.
  - 매 틱 이전 위치에서 지금 위치까지의 선분을 실드, 지형지물, 지면, 유닛 원기둥에 대해 검사하고 가장 먼저 닿은 것에서
    터진다. 쏜 유닛은 맞지 않는다.
- **피해:**
  - 한 번 맞을 때 `max(피해 - 방어력, 피해 × 0.25)`.
  - 범위 피해는 3D 구(공중 유닛은 지면 폭발에 맞지 않는다). 중심 100%에서 가장자리 25%까지 선형으로 줄고, 방어력은
    유닛마다 적용한다.
  - **아군 피해:** 총탄은 길 위의 첫 유닛을 팀과 상관없이 맞히고, 범위 피해는 범위 안의 모든 유닛에 들어간다.
  - 피해는 틱 동안 더하기만 하고, 죽음은 틱 끝에 한꺼번에 정한다. 그래서 같은 틱 안의 피해 순서가 결과를 바꾸지 않는다.

### 게임 오브젝트 구조

결정:

- **시뮬레이션은 씬 그래프 밖에 있다.** 유닛과 투사체는 `NvNode`가 아니다. `Battle` 구조체의 평평한 배열이며, 렌더링,
  에디터, 프레임 시간을 읽지 않는다. 화면은 매 프레임 그것을 읽어 노드와 이펙트로 옮긴다.
- **정의와 인스턴스를 나눈다.** `UnitDef`, `WeaponDef`는 바뀌지 않는 데이터, `Unit`, `Projectile`은 실행 상태다.
- **수명별 풀.** 분대와 유닛은 전투 시작에 만들어 끝까지 그 자리에 있다. 투사체는 짧게 살고 자주 생긴다.
- **구분된 공용체(discriminated union)는 종류가 서로 배타적이고 모양이 다른 곳에만.** 능력 상태(Jump, Shield)와
  미사일 데이터. A에는 Shield 하나뿐이지만 B의 Jump가 들어올 자리라서 처음부터 union으로 둔다. 능력의 태그는
  정의(`UnitDef.ability.kind`)에 있으므로 인스턴스에 다시 쓰지 않는다. 유닛 종류 사이의
  차이는 대부분 데이터라서 유닛 자체는 union이 아니다.
- **id.** 유닛은 전투 중에 생기지 않고 슬롯이 재사용되지 않으므로 `UnitId`는 슬롯 번호다(0 = 없음). 죽은 유닛은
  배열에 남고 `UNIT_DEAD`로 표시된다. 전투 중 생성(소환)을 더할 때 세대를 더한다.
- **투사체**는 아무도 가리키지 않으므로 사라질 때 마지막 것과 바꿔 지운다(swap-remove). 순서가 바뀌지만 결정적이다.
- **이벤트.** 시뮬레이션은 화면이 쓸 이벤트(발사, 명중, 폭발, 실드 피격, 죽음, 점프)를 틱마다 목록에 쓴다. 화면은 그것으로
  `nv_vfx_*`를 부른다. 시뮬레이션은 이벤트를 읽지 않는다. 목록이 가득 차면 이벤트만 버리고 센다.

```c
#define BATTLE_TICK_RATE        30
// The field's size and the time limit are the project file's rules (BattleRules), not macros; their upper limits are:
#define BATTLE_MAX_GRID_WIDTH   64
#define BATTLE_MAX_GRID_LENGTH  128
#define BATTLE_MAX_PROPS        256
#define BATTLE_MAX_SQUADS       128
#define BATTLE_MAX_UNITS        2048
#define BATTLE_MAX_PROJECTILES  8192
#define BATTLE_MAX_EVENTS       4096 // per tick
#define UNIT_MAX_WEAPONS        2

typedef enum UnitLayer { UNIT_LAYER_GROUND, UNIT_LAYER_AIR } UnitLayer;
typedef enum UnitSize { UNIT_SIZE_SMALL, UNIT_SIZE_MEDIUM, UNIT_SIZE_LARGE } UnitSize;
enum { TARGET_GROUND = 1 << 0, TARGET_AIR = 1 << 1 };

typedef enum ProjectileKind {
    PROJECTILE_NONE, // melee: the hit lands when the weapon fires
    PROJECTILE_BULLET,
    PROJECTILE_SHELL,
    PROJECTILE_MISSILE,
} ProjectileKind;

typedef struct WeaponDef {
    const char* name;
    ProjectileKind projectile;
    u32 targets; // TARGET_*
    f32 min_range, range;
    f32 damage, splash_radius;
    f32 cooldown;
    f32 speed;        // bullet, missile
    f32 turn_rate;    // missile, radians per second
    f32 launch_angle; // shell, radians
    f32 spread;       // meters around the aim point
    NvVec3 muzzle;    // in the unit's space
} WeaponDef;

typedef enum AbilityKind { ABILITY_NONE, ABILITY_JUMP, ABILITY_SHIELD } AbilityKind;

typedef struct AbilityDef {
    AbilityKind kind;
    union {
        struct { f32 distance, height, duration, cooldown, prop_range, melee_range; } jump;
        struct { f32 radius, capacity, regen, regen_delay; } shield;
    };
} AbilityDef;

typedef struct UnitDef {
    const char* name;
    UnitLayer layer;
    UnitSize size;
    u32 squad_size;
    u32 footprint; // cells per side
    u32 cost;
    f32 health, armor, speed, turn_rate;
    f32 radius, height, altitude;
    u32 weapon_count;
    WeaponDef weapons[UNIT_MAX_WEAPONS];
    AbilityDef ability;
} UnitDef;

typedef struct UnitId { u32 index; } UnitId; // 0 = none

enum { UNIT_DEAD = 1 << 0, UNIT_JUMPING = 1 << 1 };

typedef struct Unit {
    u32 flags;
    u8 team;
    u8 def;
    u16 squad;
    NvVec3 position, previous_position; // previous: for drawing between ticks
    NvVec3 velocity;
    f32 yaw, previous_yaw;
    f32 health;
    f32 damage_taken; // this tick; applied at the tick's end
    UnitId move_target;
    struct { UnitId target; f32 cooldown; } weapons[UNIT_MAX_WEAPONS];
    union { // tagged by UnitDef.ability.kind
        struct { f32 cooldown, time; NvVec3 from, to; } jump;
        struct { f32 energy, since_hit; } shield;
    } ability;
} Unit;

typedef struct Projectile {
    ProjectileKind kind;
    u8 team;
    u8 weapon; // index into the shooter's UnitDef.weapons
    UnitId shooter;
    NvVec3 position, previous_position, velocity;
    f32 distance_left;
    union { // tagged by kind
        struct { UnitId target; NvVec3 last_target_position; } missile;
    };
} Projectile;

typedef struct Squad {
    u8 team, def;
    u8 cell_x, cell_row; // the footprint's corner nearest the origin of the grid
    u32 first_unit, unit_count, alive_count;
} Squad;

typedef struct Prop {
    u8 cell_x, cell_row, width, length; // in cells
    f32 height;
} Prop;

typedef enum BattleEventKind {
    BATTLE_EVENT_FIRE, BATTLE_EVENT_HIT, BATTLE_EVENT_EXPLODE, BATTLE_EVENT_SHIELD_HIT, BATTLE_EVENT_DEATH, BATTLE_EVENT_JUMP,
} BattleEventKind;

typedef struct BattleEvent { // for the view only
    BattleEventKind kind;
    u8 def, weapon;
    NvVec3 position, direction;
    f32 size;
} BattleEvent;

typedef enum BattlePhase { BATTLE_DEPLOY, BATTLE_FIGHT, BATTLE_RESULT } BattlePhase;
typedef enum BattleOutcome { OUTCOME_NONE, OUTCOME_PLAYER, OUTCOME_ENEMY, OUTCOME_DRAW } BattleOutcome;

typedef struct Battle {
    BattlePhase phase;
    BattleOutcome outcome;
    u32 tick;
    u64 rng; // seeded at Start from the stage's seed
    Prop props[BATTLE_MAX_PROPS];
    u32 prop_count;
    u8 blocked[BATTLE_MAX_GRID_LENGTH][BATTLE_MAX_GRID_WIDTH]; // cells with a prop
    Squad squads[BATTLE_MAX_SQUADS];
    u32 squad_count;
    Unit units[BATTLE_MAX_UNITS]; // [0] is unused
    u32 unit_count;
    Projectile projectiles[BATTLE_MAX_PROJECTILES];
    u32 projectile_count;
    NvFlowField fields[2][2]; // [team][0 ground enemies, 1 all enemies]
    NvSpatialGrid grid;       // rebuilt every tick
    BattleEvent events[BATTLE_MAX_EVENTS];
    u32 event_count, events_dropped;
} Battle;
```

위 코드는 B까지 더한 모양이다. A의 구조체는 쓰는 것만 가진다(쓰기 전에는 두지 않는다는 코딩 표준):
- `UnitDef`: 이름, 비용, 체력, 방어력, 속도, 반지름, 높이, 무기 하나(`WeaponDef`), 능력(`AbilityDef`: `ABILITY_NONE`이나
  `ABILITY_SHIELD`). `layer`, `size`, `squad_size`, `footprint`, `altitude`, `weapon_count`는 없다.
- `WeaponDef`: 사거리, 피해, 쿨다운, 발사각, 퍼짐, 총구. 투사체 종류, 대상, 최소 사거리, 범위, 속도, 선회는 없다.
- `Unit`: 플래그, 팀, 정의, 위치와 이전 위치, 속도, 방향과 이전 방향, 체력, 이번 틱의 피해, 대상, 쿨다운(틱: `cooldown_ticks`), 능력 상태
  (`union`에 `shield`만: 에너지와 맞은 뒤의 틱 `since_hit_ticks`). 분대는 없다. 틱으로 세는 값은 초로 쓰는 정의의 값과 헷갈리지 않게 이름에
  `_ticks`를 붙인다(`docs/CODING_STANDARD.md`의 "이름 짓기").
- `Projectile`: 팀, 쏜 유닛, 위치와 이전 위치, 속도, 피해(방어력 전; 실드가 일부만 받으면 남은 만큼). 종류와 미사일 데이터는 없다.
- `Battle`: 정의, 단계, 결과, 틱, 난수, 플레이어의 배치(칸마다 유닛 종류)와 쓴 공급, 유닛, 포탄, 이벤트(발사, 명중, 실드 피격,
  죽음; 이벤트마다 팀과 유닛 종류), 분리를 위한 임시 배열. 지형지물, `blocked`, 분대, 흐름장, 공간 격자는 없다.

### 틱 순서 (B; A의 것은 "A 규칙" 끝에)

1. 모든 유닛의 `previous_position`, `previous_yaw`를 기록하고 공간 격자를 다시 만든다(유닛 슬롯 순서로 넣는다).
2. 이번 틱 차례인 흐름장을 다시 만든다.
3. 이번 틱 차례인 유닛의 대상을 다시 고른다.
4. 이동과 능력(점프 시작과 진행, 분리, 밀어내기, 적분).
5. 무기: 쿨다운, 발사(투사체 생성, 근접 피해).
6. 투사체: 적분, 충돌, 피해와 범위 피해, 실드.
7. 죽음: `damage_taken`을 적용하고 체력이 0 이하인 유닛을 `UNIT_DEAD`로 표시한다.
8. 실드 회복, 끝 조건 검사.

### 결정론

고정 30 Hz 틱, 시드 난수(PCG32, 직접 작성), 모든 반복은 슬롯 순서, 시뮬레이션은 프레임 dt를 읽지 않는다. WebAssembly의
float는 같은 빌드 안에서 결정적이고 `sinf` 같은 libm 함수도 모듈 안에 컴파일되므로, 고정소수점은 필요 없다. 끝난
전투의 상태 해시(FNV-1a)로 비교한다.

### 코드 위치

샌드박스(`sandbox/`)과 따로, 자기 실행 파일을 가진 `autobattler/` 폴더에 둔다. 엔진(`nv`)만 링크하고 `sandbox/`의 코드는 쓰지 않는다.

| 파일 | 내용 |
|---|---|
| `autobattler/CMakeLists.txt` | 실행 파일 `autobattler`. 설치 구성 요소 `autobattler`의 `<prefix>/`에 설치한다("페이지" 참고) |
| `autobattler/main.c` | 창, GPU, 렌더러, ImGui, 프로젝트 파일 읽기, 배치(레이아웃), 프레임 루프(고정 틱 누산기), 디버그 내보내기 |
| `autobattler/game.h` | 실행 파일의 상태(`Game`: 창, GPU, 렌더러, 전투, 뷰가 가진 노드와 메시)와 파일 사이의 선언 |
| `autobattler/battle.h`, `autobattler/battle.c` | 규칙과 틱. GPU와 ImGui를 포함하지 않으므로 ctest가 빌드할 수 있다 |
| `autobattler/defs.c` | 프로젝트 파일 읽기(`abproj.md`). GPU와 ImGui를 쓰지 않으므로 ctest가 빌드할 수 있다 |
| `autobattler/data/default.abproj` | 규칙, 유닛 정의, 스테이지(적 배치; B에서 지형지물) |
| `autobattler/battle_view.c` | 메시, 이펙트, 카메라, 노드와 디버그 라인으로 그리기, 배치 입력, 패널, 빌드 라벨 |
| `autobattler/strings.c` | 게임의 한국어 표("UI 문자열" 참고) |
| 공간 격자, 흐름장 (B) | 열린 질문 2: `engine/`(`engine/spatial.h`, `engine/flow.h`) 또는 `autobattler/` |

### 실행 파일

- **페이지:** 기본 브랜치의 Release는 Pages의 `autobattler/release/`, Debug는 `autobattler/debug/`, 다른 브랜치의 Debug는
  `autobattler/<branch>/`(`/`는 `-`가 된다). 샌드박스의 폴더(`release/`, `debug/`, `<branch>/`)는 그대로다. 샌드박스와 따로
  내려받는다.
  - 루트 `CMakeLists.txt`에 `add_subdirectory(autobattler)`.
  - `nv_setup_executable`에 `PRELOAD <폴더>@<패키지 안 경로> ...` 인수를 더한다(`ASSETS <폴더>`는 `<폴더>@/assets`와 같다).
    게임은 정의 파일 폴더 `autobattler/data`를 `/data`에, 글꼴 폴더 `assets/fonts`만 `/assets/fonts`에 넣는다.
    `assets/` 전체는 넣지 않는다(쓰지 않는 캐릭터 모델이 빠진다).
  - `nv_setup_executable`에 `COMPONENT <name>` 인수를 더한다(기본값 `web`). `autobattler`는
    `nv_setup_executable(autobattler ROOT COMPONENT autobattler)`이므로 샌드박스의 `web` 패키지에 섞이지 않는다.
  - `build.yml`의 Package가 `--component autobattler`를 `dist/engine-web/autobattler/release`와 `.../debug`에 설치하고,
    stage가 기본 브랜치에서는 `autobattler/release`, `autobattler/debug`를, 다른 브랜치에서는 `autobattler/<branch>`를
    바꾼다. `autobattler`라는 이름의 브랜치는 `release`, `debug`처럼 게시하지 않는다(그 폴더를 덮어쓰므로).
- **화면:** 전장이 뷰포트이고 ImGui 창 하나(Battle 패널)가 그 옆에 붙는다: 넓은 화면에서는 오른쪽(폭 300), 세로로 긴 화면에서는 아래
  (높이 42%). 뷰포트에서 시작한 입력은 ImGui를 건너뛰므로 둘은 겹치지 않는다. 에디터의 도크, 검색, 팔레트, 저장, undo, 선택, 콘솔
  탭은 없다. 씬 해상도는 뷰포트와 같게 시작하고, 터치 화면에서는 절반이다(샌드박스처럼). 구석의 빌드 라벨(빌드 종류, 커밋,
  내려받은 양, 커밋 제목)은 샌드박스와 같은 엔진 함수 `nv_imgui_draw_build_label`이 그린다(배지 없음).
- **카메라:** 플레이어 진영(-Z) 뒤 위에서 비스듬히(피치 62°, 거리 104 m, 96 m 길이의 전장 기준으로 전장 길이에 비례) 전장 전체가 보이게 본다. 왼쪽 드래그나 한 손가락은 땅 위로
  화면을 옮기고(손가락을 따라가고, 목표는 전장 안으로 제한), 오른쪽이나 가운데 버튼 드래그와 두 손가락은 돌리고, 휠과 핀치는
  확대한다(거리 15–140 m, 더 긴 전장에서는 최대 거리가 그에 따라 커진다, 피치 20°–85°).
- **그리기:**
  - 유닛은 엔진의 기본 도형(`docs/specs/mesh.md`)으로 그린 팀 색(아군 파랑, 적 빨강) 메시 노드 하나다(유닛 종류마다 메시 하나).
    A의 Crawler는 캡슐 몸통과 앞쪽의 원뿔(향하는 방향), 포탄은 작은 구다. 몸통의 반지름은 유닛의 반지름과 높이의 절반 중 작은 쪽이다
    (Crawler는 높이가 지름보다 낮아 둥근 덩어리가 된다). 노드는 처음 필요할 때 만들고, 죽은 유닛과 사라진 포탄은 메시를 떼어 숨긴다.
    틱 사이를 보간한다(방향은 짧은 쪽으로).
  - 땅과 구역 색(두 구역은 땅 위 얇은 평면)은 메시, 배치 격자(배치 단계에서만)와 마우스가 가리키는 칸의 초록/빨강 상자, 체력 바(전투
    중에만; 화면 오른쪽 방향의 선 몇 개)는 디버그 라인이다. 지형지물은 회색 상자(B).
  - 포탄은 틱마다 지난 구간에 `nv_vfx_trail`, 이벤트는 효과로: 발사는 총구의 작은 섬광, 명중은 작은 폭발과 `nv_vfx_decal`의 그을음,
    실드 피격은 파란 불꽃, 죽음은 큰 폭발(`nv_vfx_burst`). 실드는 남은 에너지에 따라 밝기가 바뀌는 디버그 라인 원(세 축) 셋이다.
    이벤트와 포탄 구간은 틱마다(한 프레임에 틱이 여럿 돌아도) 처리하고 이벤트 목록을 비운다.
  - 태양(방향광)이 그림자를 드리운다(`docs/specs/shadows.md`): 샌드박스처럼 데스크톱은 2048 맵과 High 필터, 터치 화면은 1024와 Low.
    그림자 거리는 카메라 거리의 1.6배다(`SHADOW_DISTANCE_PER_CAMERA_DISTANCE`). 처음 카메라에서 전장의 먼 구석이 카메라 거리의
    약 1.4배라 모든 유닛에 그림자가 지고, 확대해도 맵의 텍셀이 화면에서 비슷한 크기로 남는다. 옮기거나 돌려도 거리는 그대로라 그림자가
    떨리지 않는다. 설정 UI는 없다.
- **시간 조작:** 게임 시각은 실제 시각 × 속도이고 일시정지 중에는 멈춘다. 패널의 Pause와 0.5x, 1x, 2x, 4x 버튼이 바꾼다(고른 것이
  밝다; 일시정지는 속도를 기억한다; Start는 일시정지를 푼다; 저장하지 않는다). 규칙은 같은 30 Hz 틱을 돌고 속도는 한 프레임에 도는 틱
  수만 바꾸므로, 결과는 속도와 상관없이 같다. 누산기는 한 프레임에 최대 `4 × max(1, 속도)`틱만 돌리고, 더 밀리면 남은 시간을 버린다
  (느린 기기가 따라잡으려다 더 느려지지 않게). 프레임의 dt는 0.1초로 자른다. 이펙트도 같은 게임 시각을 받는다(`nv_vfx_update`): 일시정지는
  이펙트를 멈추고 배속은 빠르게 한다.
  - **틱 단위 이동:** `< Tick`과 `Tick >` 버튼이 일시정지하고 한 틱 뒤로 또는 앞으로 간다(배치 단계에서는 꺼짐; 앞으로는 전투 중에만).
    누르고 있으면 0.4초 뒤부터 프레임마다 한 틱씩 간다(ImGui의 ButtonRepeat는 느린 프레임에서 한 번 누름이 두 번이 되어 쓰지 않는다).
    앞으로는 틱 하나를 돌리고 그 틱의 이펙트에 1/30초를 준다(전투 시각은 멈춘 채). 뒤로는 `battle_seek`가 같은 배치와 시드로 라운드를
    처음부터 그 틱까지 다시 돌린다: 규칙이 결정적이므로 그때의 상태와 같다(해시가 같다). 거꾸로 돌릴 수 없는 이펙트는 지운다. 일시정지
    중에는 보간 없이 마지막 틱 그대로 그린다. 처음부터 다시 돌리는 비용은 틱 수에 비례하는데, A의 군대(유닛 수십)에는 작다. B의 큰 군대에는
    1초마다 스냅숏을 두고 가장 가까운 것부터 다시 돌린다(`// TODO:`).
- **배치 입력:** 패널에서 유닛 종류를 고르고 뷰포트의 칸을 탭하면 놓는다. 놓을 수 있는 칸은 초록, 없는 칸은 빨강으로
  미리 보여 준다(마우스가 가리키는 칸만; 터치 화면에는 없다). 놓인 유닛을 탭하면 지운다. 데스크톱과 폰 모두 같다. 배치 단계에서만.
- **Battle 패널:** 위에서 아래로: 단계, 언어 콤보, 프로젝트(파일 이름, Open..., Reload, Save, Save as...; `abproj.md`의 "불러오기와 저장"), 접힌 Rules 절(배치 중에 규칙을 고친다; `abproj.md`의 "규칙 고치기"), 유닛 종류 버튼(이름과 비용; 고른 것이 밝다), Start, Retry, Reset, 속도 버튼(Pause, 0.5x, 1x, 2x, 4x), `< Tick`과 `Tick >`, 남은 공급, 팀별
  살아 있는 유닛, 시간(초와 틱: 초는 틱 수 ÷ 30이라 느린 기기에서는 실제 시간보다 느리게 간다), (결과 단계에서) 결과와 남은 가치, 짧은 도움말. 버튼은 단계가 바뀌어도 제자리에 있다. 정의 파일을 읽지 못하면 패널은 첫 오류만 보인다. Start는 플레이어
  유닛이 있을 때, Retry는 배치 단계가 아닐 때 켜진다. Reset은 어느 단계에서든 배치를 비운다.
- **디버그 내보내기(Debug 빌드):** `_battle_debug(n)`(0 단계, 1 틱, 2 결과, 3 아군 생존, 4 적 생존, 5 해시, 6 유닛 수, 7 포탄 수, 8 남은
  공급, 9 정의 읽기 성공, 10 고른 유닛 종류, 11 속도 × 100; 일시정지면 0, 12 그림자 맵 크기; 없으면 0, 13 그림자 거리(m)), `_battle_debug_set_speed(speed)`(0은 일시정지), `_battle_debug_step(direction)`(+1 앞, -1 뒤; 일시정지), `_battle_debug_deploy(def, x, row)`, `_battle_debug_start()`, `_battle_debug_run(ticks)`,
  `_battle_debug_layout(region, component)`(0 뷰포트, 1 패널; 0 x, 1 y, 2 폭, 3 높이; CSS 픽셀), `_battle_debug_project(x, y, z, axis)`
  (월드 점이 화면 어디인가; 탭 시험용).
- **크기:** Release의 패키지는 gzip으로 `autobattler.data` 1.18 MB(글꼴 대부분)와 `.wasm` 0.29 MB다.

### UI 문자열 (엔진으로 옮김)

결정(2026-10-02): `T()`, `TL()`과 언어 선택을 엔진(`engine/strings.h`)으로 옮기고, 한국어 표는 실행 파일마다 따로 둔다. 자세한
내용은 `docs/specs/shared.md`의 "UI 문자열"이다. A부터 게임은 `autobattler/strings.c`에 자기 표를 두고, 브라우저 언어로
시작하며, Battle 패널의 Language 콤보로 바꾼다(저장 없음). 패널의 모든 글자는 `T()`/`TL()`을 거친다. B는 자기가 더하는 글자의
행을 같은 표에 더한다.

### 서드파티 후보

| 후보 | 무엇 | 언어, 라이선스 | 판단 |
|---|---|---|---|
| **직접 작성** (추천) | 공간 격자 약 150줄, 흐름장 약 200줄, 규칙 | C17 | 고정 용량, 아레나, 결정적 반복 순서를 그대로 지킨다 |
| flecs | ECS | C, MIT | 콜백과 불투명 저장소가 코딩 표준과 맞지 않는다. 오브젝트 종류가 몇 개뿐이라 이득이 없다 |
| Recast/Detour | navmesh와 군중 | C++, zlib | 전장이 평평한 칸 격자라 navmesh가 필요 없다(`autobattler.md`). 두 번째 C++ 파일이 된다 |
| **정의 데이터: 직접 만든 줄 단위 텍스트** (결정) | "정의 파일" 절의 형식과 약 150줄의 파서 | C17 | 주석을 쓸 수 있고, 아레나만 쓰며, 틀린 줄을 줄 번호와 함께 알린다. 형식을 우리가 문서로 남겨야 하고 편집기 도움이 없다 |
| 정의 데이터: C 표 | `defs.c`의 표 | C17 | 파서가 필요 없지만 값을 바꿀 때마다 다시 컴파일해야 한다 |
| 정의 데이터: JSON + jsmn | 토크나이저만 | C, MIT | 할당이 없고 편집기가 문법을 검사한다. 주석을 못 쓰고, 토큰을 구조체로 옮기는 코드는 우리 몫 |
| 정의 데이터: JSON + cJSON | 트리 파서 | C, MIT | 바로 트리로 읽힌다. 스스로 `malloc`을 하고 주석을 못 쓴다 |
| 정의 데이터: TOML + tomlc99 | 파서 | C, MIT | 주석과 섹션이 있다. 크고 스스로 `malloc`을 한다 |
| 정의 데이터: INI + inih | 파서 | C, BSD | 아주 작다. 무기와 능력 같은 중첩을 담기 어렵고 콜백 방식이다 |
| 정의 데이터: `engine/chunk.h` | 우리 바이너리 형식 | C17 | 의존성이 없지만 편집기가 필요하다 |

### 테스트

`tests/battle_test.c`(ctest, Node)가 `autobattler/battle.c`, `autobattler/defs.c`와 공간 격자(B에서 흐름장) 소스를 빌드한다.
`-sNODERAWFS=1`로 링크해 저장소의 `autobattler/data/`를 바로 읽는다.

A:
- 프로젝트 파일(`abproj.md`의 "테스트" 절): 형식은 테스트 안의 텍스트로만 시험하고(오류마다 맞는 줄 번호, 규칙 키, 버전, 최상위 순서),
  저장소의 `default.abproj`는 오류 없이 읽히고 한 라운드가 제한 시간 안에 끝나는지만 본다(값은 확인하지 않는다). 규칙을 코드에서 파일로
  옮기기 전의 라운드 해시(시드 1–4)가 그대로다.
- 같은 배치와 시드로 두 번 돌리면 같은 해시. 시드가 다르면 퍼짐이 달라진다.
- `battle_seek`: 전투 중이든 결과든 어느 틱으로 돌아가도 그 틱에 기록한 해시와 같고, 거기서 다시 돌리면 이어서 같으며, 끝을 넘겨 찾으면
  끝에서 멈추고, 배치 단계에서는 아무것도 하지 않는다.
- 규칙 장면 하나씩: 가장 가까운 적을 고른다; 포탄이 퍼짐 안에서 목표점 근처에 떨어진다(탄도 계산); 포탄이 처음 닿은 적 하나만
  깎고 아군은 지나간다; 리드 조준한 포탄이 곧게 움직이는 대상을 맞힌다; 빠르게 지나가는 포탄도 원기둥을 뚫지 않는다; 방어력 식;
  실드가 적 포탄을 막다가 에너지가 다하면 통과시키고, 맞지 않으면 다시 찬다; 에너지가 모자라면 남은 피해만 지나간다
  (에너지 10, 피해 30 → 유닛은 `max(20 - 5, 20 × 0.25)` = 15); 실드가 이웃을 노린 포탄도 막는다; 같은 틱에 양쪽이 전멸하면 무승부; 겹친 두 유닛이
  분리로 떨어진다; 한쪽이 전멸하면 끝난다; 60초에 끝나고 남은 가치로 판정한다.

B:
- 공간 격자: 질의가 무차별 검사와 같은 유닛을 같은 순서로 돌려준다.
- 흐름장: 벽을 돌아가는 경로, 막힌 칸, 목표 없음.
- 규칙 장면: 총탄이 길 위의 아군을 맞힌다; 범위 피해가 아군도 깎는다; 지면 폭발이 공중 유닛을 맞히지 않는다; Flak이 공중만
  노린다; 낮은 벽이 총탄을 막는다; Wasp가 지형지물 위를 난다; Ranger가 낮은 벽을 넘어 뛴다; 박격포의 최소 사거리.

A부터 `tests/strings_test.mjs`가 `autobattler/`의 문자열도 검사한다(ctest 명령에 `autobattler` 폴더를 더한다).

Playwright(Release, Debug, 데스크톱과 폰 크기, A와 B 끝마다; 손으로 돌리는 검사이고 스크립트는 저장소에 없다): `autobattler/` 페이지를
열고 다음을 본다. 정의 파일을 읽고 적이 놓였다; 패널이 데스크톱에서는 뷰포트 오른쪽에, 폰에서는 아래에 있다; 탭으로 놓고 지운다;
플레이어 구역 밖(적 구역, 가운데)의 탭은 아무것도 하지 않는다; 공급이 다하면(10기) 열한 번째는 거절된다; 드래그가 화면을 옮기고, 휠이
확대하고, 오른쪽 드래그가 돌린다(그 뒤에도 탭이 맞는 칸에 놓인다); 패널의 Start로 전투가 시작되고 틱이 실시간으로 가며 전투 중에는
놓을 수 없다; 속도 버튼이 속도를 바꾸고 Start가 일시정지를 푼다; 일시정지는 틱을 멈추고 4x는 1x보다 2.5배 넘게, 0.5x는 0.75배 안 되게
돈다; `Tick >`가 한 틱씩 가고 `< Tick`이 같은 해시로 돌아가며, 누르고 있으면 여러 틱을 간다; 라운드가 결과로 끝난다(속도를 바꿔 가며 돈 결과가 한 번에 돌린 결과와 해시가 같다); Retry는 같은 배치로 돌아가고 같은 배치는 같은 해시를 낸다; Reset은 배치를 비우고 비면 Start가
아무것도 하지 않는다; 좁은 창은 패널을 아래로 옮긴다; 콘솔에 오류와 경고가 없고 assert로 멈추지 않는다. Release에서는 디버그 내보내기가
없으므로 같은 흐름을 눈으로(화면 그림) 본다. 폰 크기에서 패널 위젯을 터치하면 탭으로 알려지기까지 몇 프레임이 걸리므로 약 2초를 기다린다.

### 단계

0. **선행 (끝남):** `docs/specs/shared.md`(문자열, 메시, 해상도와 탭, 카메라, 글꼴, 버전, 도우미를 엔진으로).
1. **A 시뮬레이션 (끝남):** 정의 파일(`autobattler/data/`)과 그 파서(`defs.c`), `battle.c`, `battle_test.c`의 A 항목. 화면 없음.
2. **A 실행 파일 (끝남):** `autobattler/main.c`, 카메라, 그리기, 이펙트, Battle 패널과 그 한국어 표(`autobattler/strings.c`), 배치 입력, CMake와 CI 배포.
3. **A 확인과 문서 (끝남):** Playwright 검사(디버그 내보내기는 2단계에서 이미 넣었다), `AGENTS.md`와 `autobattler.md` 갱신.
   여기서 한 번 멈추고 A를 직접 해 본다. B의 범위는 그 결과로 다시 본다.
   **프로젝트 파일 (끝남, 2026-10-05):** 두 정의 파일과 규칙 상수를 `default.abproj` 하나로(`docs/specs/abproj.md`).
4. **B:** "B 규칙"의 나머지(유닛 5종, 공중, 분대, 무기 여러 개, 총탄과 미사일, 범위 피해와 아군 피해, 지형지물, 엄폐, 흐름장,
   Jump, 공간 격자), 더한 글자의 한국어 행, B 테스트.

### 열린 질문

1. ~~**정의 데이터 형식**~~: 해결됨(2026-10-03). 직접 만든 줄 단위 텍스트 파일을 패키지에 넣는다("정의 파일" 절).
2. **공간 격자와 흐름장의 위치 (B):** `autobattler.md`대로 엔진(`engine/spatial.h`, `engine/flow.h`)인가, 아니면
   `autobattler/`에서 먼저 쓰고 두 번째 쓰임이 생길 때 옮길까?
3. ~~**시간 제한 판정**~~: 해결됨(2026-10-03). 60초가 되면 남은 가치(`비용 × 체력 / 최대 체력`의 합)로 정한다.
4. **분대:** 시작 뒤 유닛이 각자 행동하는 것(제안)이 맞는가, 대형을 유지해야 하는가?
5. **대형 유닛의 통로 (B):** 흐름장은 칸 하나 너비를 기준으로 하므로 Fortress가 1칸 틈에 낄 수 있다. B는 스테이지에
   1칸 틈을 두지 않고, 크기별 흐름장은 나중으로 미룬다. 괜찮은가?
6. ~~**수치**~~: 해결됨(2026-10-03). 표와 정의 파일의 값은 출발점으로 두고, A를 해 본 뒤 `default.abproj`에서 조정한다.
7. ~~**UI 문자열**~~: 해결됨. 엔진으로 옮긴다("UI 문자열" 절).
8. ~~**에셋**~~: 해결됨(2026-10-03). 게임 패키지에는 `PRELOAD`로 `assets/fonts/`와 `autobattler/data`만 넣는다. 캐릭터 모델
   (`assets/quaternius/`, gzip 약 0.6 MB)은 쓰지 않으므로 빠진다("실행 파일" 절).

## English

Status: A's phases 1 to 3 (the simulation and the definition files, the executable, checks and docs) built (2026-10-03), the rest a draft. Split into two stages, A and B. The definition files and the rule constants merged into one project file (2026-10-05, `abproj.md`).

### Goal

Step 1 of `docs/autobattler.md` (the battle prototype): the least of items 1 (simulation core), 2 (spatial queries), 3
(crowd movement) and 4 (data-driven definitions). One round of battle, drawn with the engine's primitive shapes, debug lines and effects.
The point is to settle the rules and the game object structure before finding out whether the game is fun.

How the rules interact is hard to check fully before building, so the prototype comes in two stages.
- **A** is the smallest version in which one round runs from start to end. One ground unit type (the Crawler), with one shell
  weapon and one Shield, fights on an open field. Its rules are all in the "Stage A rules" section.
- **B** is everything else: from "Stage B rules (the full prototype)", the five unit types, air, size classes, squads, several
  weapons and three projectile kinds, area damage and friendly fire, props and flow fields, Jump and the spatial grid.

### Scope

| A (first) | B (next) | Out (later) |
|---|---|---|
| A cell-based deployment grid (one unit per cell) | Deployment by squad | Heightmap terrain (the ground is flat at y = 0) |
| Real height: the shell's ballistic arc | Air units and altitude, 3D area damage | Multiple rounds, base health |
| One unit type (Crawler), ground only, no size classes | Five unit types, three size classes | Upgrades, tech, cards, specialists |
| One weapon per unit, one ability (Shield) | Several weapons, Jump | An AI opponent (the enemy deployment is fixed in the stage data) |
| One projectile kind (shells), armor, single-target damage | Bullets and missiles, area damage, friendly fire | |
| Nearest-enemy targeting, steering straight in with separation (every pair compared) | Props, cover, flow fields, the spatial grid | Mass rendering and animation (one mesh node each) |
| One round, ended by a wipe-out or a 60-second time limit | | Saving, undo |
| An executable of its own, `autobattler`, with a Korean and English panel (the game's Korean table) | | A game UI (one ImGui panel for now) |
| Time controls: pause, 0.5x, 1x, 2x, 4x (added after it was built) | | |

### Stage A rules

A runs from start to end on these rules alone. The values are a starting point for balancing. The field's size and zones, the
round length, gravity, the retarget interval, the stop fraction and the minimum damage fraction are values of the project file's
`rules` (`abproj.md`); the table gives today's.

| Topic | A |
|---|---|
| Field | 2 m cells, 32 cells × 48 rows. Deployment zones as in B (player rows 0–13, enemy rows 34–47). No props; the ground is y = 0. Cell (x, row) is centered at ((x + 0.5) · 2, 0, (row + 0.5) · 2) m: the player's side is −Z, the enemy's +Z |
| Deployment | One Crawler per cell. Each side has 1000 supply and a Crawler costs 100 (at most 10 per side). The enemy deployment is fixed in the stage. Start needs at least one player unit. Retry goes back to deployment with the same placement; Reset clears the player's placement |
| Units | One type, the Crawler, ground only. No size classes and no squads |
| Round | A fixed 30 Hz tick. It ends when one side is wiped out or at 60 seconds (1800 ticks). When time runs out, the side with more remaining value (the sum of `cost × health / max health`) wins; equal is a draw. Both sides wiped out on the same tick is a draw too |
| Targeting | The nearest enemy, picked again every 0.25 seconds (8 ticks) and at once when the target dies; equal distances go to the lower slot index. Distance (for targeting and range alike) is between centers on the ground (XZ) |
| Movement | Steer straight at the target and stop once it is within 90% of the range. Overlapping units push each other apart (separation, all with the same weight), and stay inside the field. Finding nearby units and separating compare every pair directly (no spatial grid). Turning is instant: a moving unit faces its velocity, a stopped one its target |
| Weapon | One. It fires when its cooldown is over and its target is in range, moving or not; cooldowns start at 0. The one projectile kind is the shell: it flies a ballistic arc under 9.8 m/s² of gravity, with the launch velocity found from the launch angle and the aim point. The aim point is on the ground: **lead aiming** (the target's position at impact, predicted from its current velocity; flight time and prediction are solved in turn three times) plus spread (uniformly random within a circle of radius `spread`) |
| Damage | A shell is a point; every tick the segment from its previous position to its current one is tested against units' upright cylinders (radius × height), so it cannot pass through. Single-target: a shell deals `max(damage - armor, damage × 0.25)` to the first **enemy** unit it touches and vanishes. It passes allies. On the ground it vanishes with no damage. Damage only adds up during a tick, and deaths are decided at its end |
| Ability | One, Shield (below) |
| Random numbers | The seed is fixed per stage (`seed` in the project file's `stage`), so the same deployment always gives the same result |

| Crawler (A) | Value |
|---|---|
| Health, armor | 120, 5 |
| Speed | 5 m/s |
| Radius / height | 0.5 / 0.8 m |
| Weapon, Lobber | Shell, range 20 m, damage 30, cooldown 1.5 s, launch angle 45°, spread 0.5 m |
| Shield | A sphere of radius 1.2 m centered at half the unit's height. It absorbs the damage of enemy shells that cross its boundary from outside (also those aimed at another unit: it guards neighbors), up to its remaining energy (capacity 60; damage before armor). A shell fully absorbed vanishes; otherwise the shield drops to 0 and the shell flies on with the damage left (the armor formula applies to the next enemy it touches). After 3 seconds without hits it refills at 10 per second. At zero energy it is off |

A's tick order: record previous positions, pick targets again, movement and separation, weapons (cooldowns, firing), shells
(integration; the first of shield, unit or ground they touch), deaths, shield refill, end conditions.

Details settled in the build (phase 1):
- **Slot order:** the player's units come first (by row, then cell), then the enemy's (in file order). Every change to the placement
  (place, remove, Reset, Retry) rebuilds the unit array from scratch. Start only starts, so Retry gives the same placement back.
- **Time in ticks:** cooldowns and the shield's wait are counted in ticks (seconds × 30, rounded up). 1.5 s is exactly 45 ticks. A
  shield counts the tick it was hit in as the first of its wait (refill starts after 3 s = 90 ticks).
- **Picking again:** every unit picks again together on the ticks where `tick % 8 == 0` (staggering them is B's work).
- **Movement:** a unit moves only as far as its stopping distance, never past it. Two units closing on each other can end up one step
  short (inside 90% of the range). Separation gathers the pushes of all pairs and applies them once at the end (order does not matter).
  Two units on the same spot part along X, the lower slot to the left.
- **Shells:** gravity is applied exactly each tick (`−½ g dt²` on the position, `−g dt` on the velocity), so a shell lands where it was
  solved to. A shell a shield took only part of goes on from the shield's boundary with the damage left and hits what it touches next.
  When the shell pool (4096) is full a weapon waits, ready, for a slot.
- **Events:** not cleared each tick. The view sets `event_count` back to 0 after reading (several ticks may run in one frame).
- **Definition errors:** `BattleDefs` holds `error_count` and the first error's text (`first_error`) for the panel to show. A missing key
  is reported at its block header's line; errors about a whole file (no `supply`, no unit or `place` at all) have no line. A supply
  overrun is reported at the first `place` that takes the total over. A key that was given with a bad value counts as given, so it
  does not add a "missing" error.

### Definition files

Decided (2026-10-03): unit definitions and the stage live in text files, not in code. The format is line-based text, and we write
the code that reads it (no new third-party code; see "Third-party candidates"). The files go into the package and are read once at
start. Changing a value needs repackaging (`cmake --build`), but no C code recompiles.

Changed (2026-10-05): the two files (`units.txt`, `stage.txt`) and the rule constants that were in code (cell size, grid, zones,
round length, gravity, the retarget interval, the stop fraction, the minimum damage fraction) merged into **one project file**,
`autobattler/data/default.abproj`. The syntax, the key tables of `rules`, `unit` and `stage`, the versions, reading and errors, and
the tests are in `docs/specs/abproj.md`. The function that reads it is `defs_read_project` (`autobattler/defs.c`). The keys B adds
(`layer`, `size`, `squad`, `footprint`, `altitude`, several `weapon`s, `ability jump`, `prop` and so on) are listed there too, under
"Later".

The values in the "Stage A rules" tables and in the Crawler table below are what `default.abproj` holds now.

### Stage B rules (the full prototype)

B is A with all of the rules below added. A's Crawler becomes the melee Crawler of the table below (squads of 16), and the shell
weapon and Shield take the values of B's tables.

#### Battlefield

| Topic | Value |
|---|---|
| Coordinates | Right-handed, Y up (`engine/math.h`). The field is centered on the origin; the player's side is +Z, the enemy's −Z |
| Cells | 2 m squares. The field is 32 cells (X) × 48 rows (Z) = 64 m × 96 m. Row 0 is the player's edge |
| Deployment zones | Player: rows 0–13; enemy: rows 34–47. The 20 rows between take no deployment |
| Props | Boxes aligned to cells (a cell range and a height). Indestructible. They block ground movement and stop projectiles that pass through the box. Their cells take no deployment |
| Prop heights | Low wall 1.5 m (cover: stops direct ground fire, but can be jumped), rock 5 m, tower 8 m. All below the air altitude (10 m) |
| Ground | y = 0. Ground units stay at y = 0 unless jumping; air units fly at their definition's altitude |

#### Round

1. **Deploy:** each side has 1000 supply. The player places and removes squads in their zone; the enemy deployment is fixed
   in the stage. Start begins the battle.
2. **Battle:** a fixed 30 Hz tick, at most 1800 ticks (60 seconds). No input.
3. **Result:** when all of one side's units are dead, the other side wins (both at once is a draw). When time runs out, the
   side with more remaining value (the sum, over units, of `squad cost / squad size × health / max health`) wins; equal
   value is a draw. Retry goes back to deployment with the same deployment; Reset clears the player's deployment.

#### Units

A squad is the unit of deployment: it takes its definition's footprint in cells, and when the battle starts its units are
placed on a grid inside those cells. From then on each unit moves and fights on its own (no formation). The squad only counts
its living units.

The values are a starting point for balancing.

| | Crawler | Ranger | Mortar | Wasp | Fortress |
|---|---|---|---|---|---|
| Layer | Ground | Ground | Ground | Air (altitude 10 m) | Ground |
| Size class | Small | Medium | Medium | Small | Large |
| Squad size | 16 | 4 | 2 | 6 | 1 |
| Footprint (cells) | 2×2 | 2×2 | 2×2 | 2×2 | 3×3 |
| Cost | 100 | 150 | 200 | 200 | 400 |
| Health | 80 | 400 | 500 | 250 | 6000 |
| Armor | 0 | 5 | 10 | 0 | 40 |
| Speed (m/s) | 7 | 4 | 3 | 8 | 2 |
| Radius / height (m) | 0.5 / 0.8 | 0.8 / 2 | 1.2 / 2 | 0.8 / 0.6 | 2.5 / 5 |
| Weapons | Claws | Rifle | Mortar | Rockets | Cannon, Flak |
| Ability | None | Jump | None | None | Shield |

The size class sets the weight in separation (pushing): small 1, medium 4, large 20. Large units push small ones aside.

#### Weapons

| Weapon | Projectile | Targets | Range (m) | Damage | Splash (m) | Cooldown (s) | Other |
|---|---|---|---|---|---|---|---|
| Claws | Melee (no projectile) | Ground | 1.2 | 25 | – | 0.6 | |
| Rifle | Bullet | Ground, air | 25 | 40 | – | 1.0 | Speed 90 m/s, spread 0.3 m |
| Mortar | Shell | Ground | 10–45 | 120 | 4 | 3.0 | Launch angle 60°, spread 1.5 m |
| Rockets | Missile | Ground | 12 | 35 | 1.5 | 0.8 | Speed 30 m/s, turn 3 rad/s |
| Cannon | Shell | Ground | 35 | 400 | 5 | 4.0 | Launch angle 15°, spread 1 m |
| Flak | Missile | Air | 30 | 60 | 3 | 1.5 | Speed 50 m/s, turn 5 rad/s |

#### Abilities

- **Jump (Ranger):** when a prop is within 4 m along its movement, or a small enemy melee unit comes within 3 m, it jumps 10 m
  along its movement over 0.8 seconds, on a parabola 3 m high. It does not jump if the landing point is a prop cell.
  Cooldown 10 seconds. While jumping it does not fire and is not pushed.
- **Shield (Fortress):** a sphere of radius 7 m. It removes enemy projectiles that cross its boundary from outside and loses
  their damage as energy (capacity 1500). After 4 seconds without hits it refills at 100 per second. Melee and friendly
  projectiles pass. At zero energy it is off.

#### Combat rules

- **Targeting:** the nearest enemy first.
  - Each weapon picks the nearest enemy within its targets (ground, air) and range.
  - A unit's move target is the nearest enemy any of its weapons can hit (no distance limit).
  - Targets are picked again every 0.25 seconds (8 ticks), spread over ticks by unit, and at once when a target dies.
  - Equal distances go to the lower slot index.
- **Movement:**
  - Ground: if the move target is within 8 m with no prop between, steer straight at it; otherwise follow the flow field. Stop
    when the move target is within 90% of the weapon's range. Separate from neighbors, and push out of prop cells and back
    inside the field. The body turns toward its movement (weapons are 360° turrets).
  - Air: ignore props, hold altitude and fly straight at the move target. Separate from other air units only.
- **Flow fields:** two per team (ground enemies only, all enemies; air enemies projected onto the ground). Goal cells are the
  cells enemies stand in. Eight-way Dijkstra with integer costs 10/14; prop cells cannot be crossed, and diagonals do not cut
  corners. Rebuilt every 10 ticks, the four fields spread over ticks. Units that hit only ground (Crawler, Mortar) follow
  "ground enemies only".
- **Firing:** a weapon fires when its cooldown is over and its target is in range. Direct fire (bullets) fires only when no
  prop lies between the muzzle and the target's center; shells and missiles skip that check. Shells lead their aim as in A;
  bullets and missiles do not, aiming at the target's position when they fire.
- **Projectiles:**
  - Bullets fly straight and vanish after their range.
  - Shells fly under gravity (9.8 m/s²); the launch velocity comes from the launch angle and the aim point (the target's
    position predicted by lead aiming, plus seeded random spread).
  - Missiles turn toward their target, and toward its last position once it dies.
  - Every tick the segment from the previous position to the current one is tested against shields, props, the ground and
    unit cylinders, and the projectile hits the first thing it touches. A shooter is never hit by its own projectile.
- **Damage:**
  - Each hit deals `max(damage - armor, damage × 0.25)`.
  - Area damage is a 3D sphere (a ground explosion does not reach air units). It falls linearly from 100% at the center to
    25% at the edge, with armor applied per unit.
  - **Friendly fire:** a bullet hits the first unit on its path whatever its team, and area damage hits every unit inside.
  - Damage only adds up during a tick, and deaths are decided together at the tick's end, so the order of damage within a tick
    does not change the result.

### Game object structure

Decisions:

- **The simulation lives outside the scene graph.** Units and projectiles are not `NvNode`s. They are flat arrays in a
  `Battle` struct that reads no rendering, editor state or frame time. The view reads it every frame and copies it into nodes
  and effects.
- **Definitions apart from instances.** `UnitDef` and `WeaponDef` are unchanging data; `Unit` and `Projectile` are runtime
  state.
- **Pools by lifetime.** Squads and units are made when the battle starts and stay in place until it ends. Projectiles are
  short-lived and made often.
- **Discriminated unions only where kinds are mutually exclusive and differently shaped:** ability state (Jump, Shield) and
  missile data. A has Shield alone, but B's Jump comes into the same place, so it is a union from the start. An ability's tag
  lives in the definition (`UnitDef.ability.kind`), so instances do not repeat it. Unit types
  differ mostly in data, so a unit itself is not a union.
- **Ids.** Units are not created during a battle and slots are not reused, so a `UnitId` is a slot index (0 = none). Dead units
  stay in the array, marked `UNIT_DEAD`. A generation gets added when creation during battle (summons) does.
- **Projectiles** are referred to by nothing, so a finished one is swapped with the last (swap-remove). That changes the
  order, deterministically.
- **Events.** Every tick the simulation writes events for the view (fire, hit, explosion, shield hit, death, jump) into a list,
  and the view turns them into `nv_vfx_*` calls. The simulation never reads events. When the list is full, only events are
  dropped, and counted.

```c
#define BATTLE_TICK_RATE        30
// The field's size and the time limit are the project file's rules (BattleRules), not macros; their upper limits are:
#define BATTLE_MAX_GRID_WIDTH   64
#define BATTLE_MAX_GRID_LENGTH  128
#define BATTLE_MAX_PROPS        256
#define BATTLE_MAX_SQUADS       128
#define BATTLE_MAX_UNITS        2048
#define BATTLE_MAX_PROJECTILES  8192
#define BATTLE_MAX_EVENTS       4096 // per tick
#define UNIT_MAX_WEAPONS        2

typedef enum UnitLayer { UNIT_LAYER_GROUND, UNIT_LAYER_AIR } UnitLayer;
typedef enum UnitSize { UNIT_SIZE_SMALL, UNIT_SIZE_MEDIUM, UNIT_SIZE_LARGE } UnitSize;
enum { TARGET_GROUND = 1 << 0, TARGET_AIR = 1 << 1 };

typedef enum ProjectileKind {
    PROJECTILE_NONE, // melee: the hit lands when the weapon fires
    PROJECTILE_BULLET,
    PROJECTILE_SHELL,
    PROJECTILE_MISSILE,
} ProjectileKind;

typedef struct WeaponDef {
    const char* name;
    ProjectileKind projectile;
    u32 targets; // TARGET_*
    f32 min_range, range;
    f32 damage, splash_radius;
    f32 cooldown;
    f32 speed;        // bullet, missile
    f32 turn_rate;    // missile, radians per second
    f32 launch_angle; // shell, radians
    f32 spread;       // meters around the aim point
    NvVec3 muzzle;    // in the unit's space
} WeaponDef;

typedef enum AbilityKind { ABILITY_NONE, ABILITY_JUMP, ABILITY_SHIELD } AbilityKind;

typedef struct AbilityDef {
    AbilityKind kind;
    union {
        struct { f32 distance, height, duration, cooldown, prop_range, melee_range; } jump;
        struct { f32 radius, capacity, regen, regen_delay; } shield;
    };
} AbilityDef;

typedef struct UnitDef {
    const char* name;
    UnitLayer layer;
    UnitSize size;
    u32 squad_size;
    u32 footprint; // cells per side
    u32 cost;
    f32 health, armor, speed, turn_rate;
    f32 radius, height, altitude;
    u32 weapon_count;
    WeaponDef weapons[UNIT_MAX_WEAPONS];
    AbilityDef ability;
} UnitDef;

typedef struct UnitId { u32 index; } UnitId; // 0 = none

enum { UNIT_DEAD = 1 << 0, UNIT_JUMPING = 1 << 1 };

typedef struct Unit {
    u32 flags;
    u8 team;
    u8 def;
    u16 squad;
    NvVec3 position, previous_position; // previous: for drawing between ticks
    NvVec3 velocity;
    f32 yaw, previous_yaw;
    f32 health;
    f32 damage_taken; // this tick; applied at the tick's end
    UnitId move_target;
    struct { UnitId target; f32 cooldown; } weapons[UNIT_MAX_WEAPONS];
    union { // tagged by UnitDef.ability.kind
        struct { f32 cooldown, time; NvVec3 from, to; } jump;
        struct { f32 energy, since_hit; } shield;
    } ability;
} Unit;

typedef struct Projectile {
    ProjectileKind kind;
    u8 team;
    u8 weapon; // index into the shooter's UnitDef.weapons
    UnitId shooter;
    NvVec3 position, previous_position, velocity;
    f32 distance_left;
    union { // tagged by kind
        struct { UnitId target; NvVec3 last_target_position; } missile;
    };
} Projectile;

typedef struct Squad {
    u8 team, def;
    u8 cell_x, cell_row; // the footprint's corner nearest the origin of the grid
    u32 first_unit, unit_count, alive_count;
} Squad;

typedef struct Prop {
    u8 cell_x, cell_row, width, length; // in cells
    f32 height;
} Prop;

typedef enum BattleEventKind {
    BATTLE_EVENT_FIRE, BATTLE_EVENT_HIT, BATTLE_EVENT_EXPLODE, BATTLE_EVENT_SHIELD_HIT, BATTLE_EVENT_DEATH, BATTLE_EVENT_JUMP,
} BattleEventKind;

typedef struct BattleEvent { // for the view only
    BattleEventKind kind;
    u8 def, weapon;
    NvVec3 position, direction;
    f32 size;
} BattleEvent;

typedef enum BattlePhase { BATTLE_DEPLOY, BATTLE_FIGHT, BATTLE_RESULT } BattlePhase;
typedef enum BattleOutcome { OUTCOME_NONE, OUTCOME_PLAYER, OUTCOME_ENEMY, OUTCOME_DRAW } BattleOutcome;

typedef struct Battle {
    BattlePhase phase;
    BattleOutcome outcome;
    u32 tick;
    u64 rng; // seeded at Start from the stage's seed
    Prop props[BATTLE_MAX_PROPS];
    u32 prop_count;
    u8 blocked[BATTLE_MAX_GRID_LENGTH][BATTLE_MAX_GRID_WIDTH]; // cells with a prop
    Squad squads[BATTLE_MAX_SQUADS];
    u32 squad_count;
    Unit units[BATTLE_MAX_UNITS]; // [0] is unused
    u32 unit_count;
    Projectile projectiles[BATTLE_MAX_PROJECTILES];
    u32 projectile_count;
    NvFlowField fields[2][2]; // [team][0 ground enemies, 1 all enemies]
    NvSpatialGrid grid;       // rebuilt every tick
    BattleEvent events[BATTLE_MAX_EVENTS];
    u32 event_count, events_dropped;
} Battle;
```

The code above is the shape with B added. A's structs hold only what A uses (the coding standard's "nothing before it is used"):
- `UnitDef`: name, cost, health, armor, speed, radius, height, one weapon (`WeaponDef`), the ability (`AbilityDef`: `ABILITY_NONE` or
  `ABILITY_SHIELD`). No `layer`, `size`, `squad_size`, `footprint`, `altitude` or `weapon_count`.
- `WeaponDef`: range, damage, cooldown, launch angle, spread, muzzle. No projectile kind, targets, minimum range, splash, speed or
  turn rate.
- `Unit`: flags, team, definition, position and previous position, velocity, yaw and previous yaw, health, this tick's damage,
  target, cooldown (in ticks: `cooldown_ticks`), ability state (a `union` with only `shield`: its energy and the ticks since a hit,
  `since_hit_ticks`). No squad. Values counted in ticks carry `_ticks` in their names, so they are not mistaken for the definitions'
  values in seconds ("Naming" in `docs/CODING_STANDARD.md`).
- `Projectile`: team, shooter, position and previous position, velocity, damage (before armor; what a shield leaves when it takes only
  part). No kind and no missile data.
- `Battle`: the definitions, phase, outcome, tick, random numbers, the player's placement (a unit type per cell) and the supply used, units,
  shells, events (fire, hit, shield hit, death; each with the team and unit type it is about), a scratch array for separation. No props,
  `blocked`, squads, flow fields or spatial grid.

### Tick order (B; A's is at the end of "Stage A rules")

1. Record every unit's `previous_position` and `previous_yaw`, and rebuild the spatial grid (units go in by slot order).
2. Rebuild the flow field whose turn it is.
3. Pick targets again for the units whose turn it is.
4. Movement and abilities (starting and advancing jumps, separation, pushing out, integration).
5. Weapons: cooldowns, firing (new projectiles, melee damage).
6. Projectiles: integration, collisions, damage and area damage, shields.
7. Deaths: apply `damage_taken`, and mark units at zero health or below `UNIT_DEAD`.
8. Shield refill, end conditions.

### Determinism

A fixed 30 Hz tick, seeded random numbers (PCG32, written here), every loop in slot order, and no frame dt read by the
simulation. WebAssembly floats are deterministic within one build, and libm functions such as `sinf` are compiled into the
module, so no fixed-point math is needed. Finished battles are compared by a hash (FNV-1a) of their state.

### Where the code goes

In a folder of its own, `autobattler/`, with its own executable, apart from the sandbox (`sandbox/`). It links only the engine
(`nv`) and uses no code from `sandbox/`.

| File | Contents |
|---|---|
| `autobattler/CMakeLists.txt` | The `autobattler` executable, installed into `<prefix>/` of the `autobattler` install component (see "Page") |
| `autobattler/main.c` | Window, GPU, renderer, ImGui, reading the definition files, the layout, the frame loop (a fixed-tick accumulator), debug exports |
| `autobattler/game.h` | The executable's state (`Game`: window, GPU, renderer, the battle, the nodes and meshes the view keeps) and the declarations between files |
| `autobattler/battle.h`, `autobattler/battle.c` | The rules and the tick. No GPU or ImGui, so ctest can build it |
| `autobattler/defs.c` | Reading the definition files (see "Definition files"). No GPU or ImGui, so ctest can build it |
| `autobattler/data/default.abproj` | The rules, the unit definitions and the stage (the enemy deployment; props in B) |
| `autobattler/battle_view.c` | Meshes, effects, the camera, drawing with nodes and debug lines, deployment input, the panel, the build label |
| `autobattler/strings.c` | The game's Korean table (see "UI strings") |
| Spatial grid, flow field (B) | Open question 2: `engine/` (`engine/spatial.h`, `engine/flow.h`) or `autobattler/` |

### Executable

- **Page:** the default branch's Release at `autobattler/release/` on Pages and its Debug at `autobattler/debug/`; other
  branches' Debug at `autobattler/<branch>/` (`/` becomes `-`). The sandbox's folders (`release/`, `debug/`, `<branch>/`)
  stay as they are. Downloaded apart from the sandbox.
  - The root `CMakeLists.txt` gets `add_subdirectory(autobattler)`.
  - `nv_setup_executable` gets a `PRELOAD <dir>@<path in the package> ...` argument (`ASSETS <dir>` is `<dir>@/assets`). The game
    puts the definition folder `autobattler/data` at `/data` and only the font folder `assets/fonts` at `/assets/fonts`, not all of
    `assets/` (the unused character models stay out).
  - `nv_setup_executable` gets a `COMPONENT <name>` argument (default `web`). The game is
    `nv_setup_executable(autobattler ROOT COMPONENT autobattler)`, so it stays out of the sandbox's `web` package.
  - In `build.yml`, Package installs `--component autobattler` into `dist/engine-web/autobattler/release` and `.../debug`,
    and stage replaces `autobattler/release` and `autobattler/debug` for the default branch, `autobattler/<branch>` for others.
    A branch named `autobattler` is not published, like `release` and `debug` (it would replace that folder).
- **Screen:** the battlefield is the viewport, with one ImGui window (the Battle panel) beside it: on its right (300 wide) on a wide
  screen, under it (42% high) on a tall one. They do not overlap, since input that starts in the viewport skips ImGui. None of the
  editor's docks, search, palette, saving, undo, selection or Console tab. The scene resolution starts equal to the viewport, and
  half of it on a touch screen (as in the sandbox). The build label in the corner (build type, commit, what was downloaded,
  the commit's subject) is drawn by the engine's `nv_imgui_draw_build_label`, as the sandbox's is (no badge).
- **Camera:** an angled view from above and behind the player's side (-Z) at 62° pitch and 104 m, so that all of the field is in view. A
  left drag or one finger moves the view over the ground (it follows the finger; the target is kept inside the field), a right or middle
  drag or two fingers turn it, the wheel and pinch zoom (15 to 140 m, pitch 20° to 85°).
- **Drawing:**
  - A unit is one mesh node in its team's color (player blue, enemy red), with one mesh per unit type, made from the engine's
    primitives (`docs/specs/mesh.md`). A's Crawler is a capsule body with a cone in front (its facing), and a shell is a small
    sphere. The body's radius is the smaller of the unit's radius and half its height (a Crawler is lower than it is wide, so it is a
    rounded lump). Nodes are made when first needed; a dead unit's and a gone shell's mesh is taken off to hide it. Interpolated
    between ticks (angles the short way round).
  - The ground and the zones' colors (the zones are thin planes just above the ground) are meshes; the deployment grid (while
    deploying), the green or red box on the cell under the mouse pointer, and health bars (while fighting; a few lines level with
    the screen) are debug lines. Props are gray boxes (B).
  - Each shell leaves `nv_vfx_trail` for the stretch it moved each tick. Events become effects: a shot is a small flash at the
    muzzle, a hit a small explosion and a scorch (`nv_vfx_decal`), a shield hit blue sparks, a death a big explosion (`nv_vfx_burst`).
    A shield is three debug-line circles (one per axis) whose brightness follows its energy left. Events and shell stretches are
    handled after every tick (several may run in one frame), and the event list is emptied then.
  - The sun (a directional light) casts shadows (`docs/specs/shadows.md`): as in the sandbox, a 2048 map with the High filter on
    desktop, 1024 and Low on a touch screen. The shadow distance is 1.6 times the camera's distance
    (`SHADOW_DISTANCE_PER_CAMERA_DISTANCE`). From the starting camera the field's far corners are about 1.4 times its distance away, so
    every unit is shadowed, and the map's texels keep about the same size on screen as the camera zooms. Moving or turning the view
    keeps the distance, so shadows do not shimmer. There is no settings UI.
- **Time controls:** the game clock is real time times the speed, stopped while paused. The panel's Pause, 0.5x, 1x, 2x and 4x
  buttons change it (the chosen one is lit; a pause keeps the speed; Start un-pauses; not saved). The rules run the same 30 Hz ticks
  and a speed only changes how many run in a frame, so the result is the same at any speed. The accumulator runs at most
  `4 × max(1, speed)` ticks in a frame and drops the time left over beyond that (so a slow device does not slow down further trying
  to catch up). A frame's dt is cut to 0.1 s. The effects get the same game clock (`nv_vfx_update`): a pause freezes them and a
  speed-up hurries them.
  - **Stepping a tick:** the `< Tick` and `Tick >` buttons pause and go one tick back or forward (off while deploying; forward only while
    fighting). Held, they go one tick a frame after 0.4 s (not ImGui's ButtonRepeat, which on a slow frame turns one click into two).
    Forward runs one tick and gives its effects 1/30 s (the battle's clock stays paused). Back is `battle_seek`, which plays the round
    again from its start with the same placement and seed up to that tick: the rules are deterministic, so it is the state the round
    had then (the same hash). Effects, which cannot run backward, are cleared. While paused the view draws the last tick as it is, with
    no interpolation. Replaying from the start costs time in proportion to the tick, which is small for A's armies (a few dozen units);
    for B's large ones, keep a snapshot every second and replay from the nearest (`// TODO:`).
- **Deployment input:** pick a unit type in the panel, then tap a cell in the viewport to place it. Cells that can take it
  preview green, others red (only the cell under the mouse pointer; a touch screen has no preview). Tapping a placed unit removes it.
  The same on desktop and phone. Only while deploying.
- **Battle panel:** top to bottom: the phase, a language combo, the project (its file name, Open..., Reload, Save, Save as...; "Loading and
  saving" in `abproj.md`), a closed Rules section (the rules edited during deployment; "Editing the rules" in `abproj.md`), a
  button per unit type (name and cost; the chosen one is lit), Start,
  Retry, Reset, the speed buttons (Pause, 0.5x, 1x, 2x, 4x), `< Tick` and `Tick >`, remaining supply, living units per team, the time (seconds and ticks: the seconds are the tick count over 30, so on a slow device they run behind real time), (in the result phase) the result and the value left, a short help
  text. The buttons stay where they are as the phase changes. When the
  definition files could not be read the panel shows only the first error. Start is on when the player has a unit, Retry when not
  deploying; Reset clears the placement in any phase.
- **Debug exports (Debug builds):** `_battle_debug(n)` (0 phase, 1 tick, 2 outcome, 3 player units alive, 4 enemy units alive, 5 hash,
  6 units, 7 shells, 8 supply left, 9 definitions loaded, 10 chosen unit type, 11 the speed × 100, 0 while paused, 12 the shadow
  map's size, 0 without one, 13 the shadow distance in meters), `_battle_debug_set_speed(speed)` (0 pauses), `_battle_debug_step(direction)` (+1 forward, -1 back; it pauses), `_battle_debug_deploy(def, x, row)`,
  `_battle_debug_start()`, `_battle_debug_run(ticks)`, `_battle_debug_layout(region, component)` (region 0 viewport, 1 panel; 0 x,
  1 y, 2 width, 3 height; CSS pixels) and `_battle_debug_project(x, y, z, axis)` (where a world point is on screen, for tap tests).
- **Size:** the Release package is, gzipped, `autobattler.data` 1.18 MB (mostly the font) and the `.wasm` 0.29 MB.

### UI strings (moved to the engine)

Decided (2026-10-02): `T()`, `TL()` and language selection move to the engine (`engine/strings.h`), and each executable keeps its
own Korean table. The details are in "UI strings" of `docs/specs/shared.md`. From A on the game keeps its table in
`autobattler/strings.c`, starts in the browser's language and switches with a Language combo in the Battle panel (not saved).
Every text in the panel goes through `T()`/`TL()`. B adds rows for the text it adds to the same table.

### Third-party candidates

| Candidate | What it is | Language, license | Judgment |
|---|---|---|---|
| **Write it ourselves** (recommended) | Spatial grid about 150 lines, flow field about 200, the rules | C17 | Keeps fixed capacities, arenas and a deterministic iteration order as they are |
| flecs | An ECS | C, MIT | Callbacks and opaque storage clash with the coding standard; with a handful of object kinds there is no gain |
| Recast/Detour | Navmesh and crowds | C++, zlib | The field is a flat cell grid, so no navmesh is needed (`autobattler.md`). It would be a second C++ file |
| **Definitions: our own line-based text** (decided) | The format of "Definition files" and a parser of about 150 lines | C17 | Allows comments, uses only arenas, reports bad lines with their numbers. We document the format ourselves, and editors give no help |
| Definitions: C tables | Tables in `defs.c` | C17 | No parser, but every value change needs a recompile |
| Definitions: JSON + jsmn | A tokenizer only | C, MIT | No allocation, and editors check the syntax. No comments, and moving tokens into structs is ours |
| Definitions: JSON + cJSON | A tree parser | C, MIT | Reads straight into a tree. Calls `malloc` itself, no comments |
| Definitions: TOML + tomlc99 | A parser | C, MIT | Comments and sections. Large, and calls `malloc` itself |
| Definitions: INI + inih | A parser | C, BSD | Very small. Nesting such as weapons and abilities is awkward, and it reads through callbacks |
| Definitions: `engine/chunk.h` | Our binary format | C17 | No dependency, but needs an editor |

### Tests

`tests/battle_test.c` (ctest, Node) builds `autobattler/battle.c`, `autobattler/defs.c` and the spatial grid (and in B the flow
field) sources. It links with `-sNODERAWFS=1` to read the repository's `autobattler/data/` directly.

A:
- The project file ("Tests" in `abproj.md`): the format is tried only with text inside the test (each error with the right line
  number, the rules' keys, the version, the top-level order); the repository's `default.abproj` is only checked to read without errors
  and to play a round that ends within its time limit (its values are not checked). Rounds of seeds 1 to 4 give the hashes they gave
  before the rules moved from code to the file.
- The same deployment and seed run twice give the same hash; a different seed changes the spread.
- `battle_seek`: going back to any tick, from the fight or the result, gives the hash recorded at that tick, running on from there
  stays the same, seeking past the end stops at the end, and in deployment it does nothing.
- One scene per rule: the nearest enemy is picked; a shell lands near its aim point, within the spread (the ballistic solve); a
  shell hurts only the first enemy it touches and passes allies; a lead-aimed shell hits a target moving in a straight line; a fast shell does not pass
  through a cylinder; the armor formula; a shield stops enemy shells until its energy runs out, then lets them through, and
  refills when not hit; short of energy, only the damage left goes through (energy 10, damage 30 → the unit takes
  `max(20 - 5, 20 × 0.25)` = 15); a shield stops shells aimed at a neighbor; both sides wiped out on one tick is a draw; two overlapping units separate; the battle ends when a side is
  wiped out; it ends at 60 seconds and is judged by remaining value.

B:
- Spatial grid: queries return the same units, in the same order, as a brute-force check.
- Flow field: a path around a wall, blocked cells, no goal.
- Rule scenes: a bullet hits an ally on its path; area damage hurts allies too; a ground explosion does not hit air units; Flak
  aims at air only; a low wall stops bullets; a Wasp flies over props; a Ranger jumps a low wall; the mortar's minimum range.

From A on, `tests/strings_test.mjs` checks `autobattler/`'s strings too (the ctest command gets the `autobattler` folder).

Playwright (Release and Debug, desktop and phone sizes, at the end of A and of B; run by hand, the script is not in the repository): open the
`autobattler/` page and check: the definition files are read and the enemy is placed; the panel is right of the viewport on desktop and
under it on a phone; taps place and remove units; taps outside the player's zone (the enemy's zone, the middle) do nothing; when the supply
runs out (10 units) an eleventh is refused; a drag moves the view, the wheel zooms and a right drag turns it (taps still land on the right cell
after); Start in the panel starts the fight, ticks advance in real time and nothing can be placed during it; the speed buttons set the speed and Start un-pauses; a pause stops the ticks, 4x runs more
than 2.5 times as many as 1x and 0.5x less than 0.75 times; `Tick >` goes one tick at a time and `< Tick` comes back to the same hash, and holding it
goes back several ticks; the round ends in a result (run at changing speeds, it gives the same hash
as a run at once);
Retry goes back to the same placement and the same placement gives the same hash; Reset clears the placement and Start with none does
nothing; a narrow window moves the panel under the viewport; no console errors or warnings and no assert stops it. The Release build has
no debug exports, so the same flow is checked by eye (a screenshot). On a phone-sized screen a touch on a panel widget takes a few frames
to count as a tap, so the checks wait about two seconds.

### Phases

0. **First (done):** `docs/specs/shared.md` (strings, meshes, resolution and taps, the camera, the font, the version and helpers
   move to the engine).
1. **A, simulation (done):** the definition files (`autobattler/data/`) and their parser (`defs.c`), `battle.c`, A's items in
   `battle_test.c`. No view.
2. **A, executable (done):** `autobattler/main.c`, the camera, drawing, effects, the Battle panel and its Korean table (`autobattler/strings.c`),
   deployment input, CMake and the CI deployment.
3. **A, checks and docs (done):** Playwright checks (the debug exports were already added in phase 2), updates to `AGENTS.md` and
   `autobattler.md`. Stop here and play A. B's scope is looked at again with what that shows.
   **The project file (done, 2026-10-05):** the two definition files and the rule constants into one `default.abproj`
   (`docs/specs/abproj.md`).
4. **B:** the rest of "Stage B rules" (five unit types, air, squads, several weapons, bullets and missiles, area damage and
   friendly fire, props, cover, flow fields, Jump, the spatial grid), Korean rows for the added text, B's tests.

### Open questions

1. ~~**Definition data format**~~: resolved (2026-10-03). Our own line-based text files, packaged ("Definition files" section).
2. **Where the spatial grid and flow field go (B):** the engine (`engine/spatial.h`, `engine/flow.h`), as `autobattler.md`
   says, or `autobattler/` first, moving them when a second use appears?
3. ~~**Time-limit judgment**~~: resolved (2026-10-03). At 60 seconds, remaining value (the sum of `cost × health / max health`) decides.
4. **Squads:** units act on their own after the start (proposed), or keep a formation?
5. **Passages for large units (B):** the flow field assumes a one-cell width, so a Fortress can get stuck in a one-cell gap. B
   keeps one-cell gaps out of the stage and leaves per-size flow fields for later. Is that acceptable?
6. ~~**Numbers**~~: resolved (2026-10-03). The values in the tables and definition files are a starting point, adjusted in
   `default.abproj` after playing A.
7. ~~**UI strings**~~: resolved. They move to the engine ("UI strings" section).
8. ~~**Assets**~~: resolved (2026-10-03). The game's package holds only `assets/fonts/` and `autobattler/data`, through
   `PRELOAD`. The character models (`assets/quaternius/`, about 0.6 MB gzip) are unused and left out ("Executable" section).
