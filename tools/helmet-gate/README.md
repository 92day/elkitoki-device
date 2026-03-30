# Helmet Gate

이 폴더는 `helmet_gate.py`를 엘키토키 작업공간 안에서 **별도 실행 도구**로 관리하기 위한 구성입니다.

## 왜 별도 실행으로 두었나

이 코드는:

- 카메라를 직접 여는 실시간 OpenCV 앱
- 전체화면 GUI
- `winsound` 기반 Windows 전용 알림음
- Hugging Face 모델 다운로드

구조라서, 현재 엘키토키 FastAPI 서버 안에 바로 넣기보다는 **독립 실행 도구**로 두는 편이 안정적입니다.

필요하면 나중에 다음 방식으로 연동할 수 있습니다.

- 헬멧 미착용 감지 시 엘키토키 서버 알림 API 호출
- 별도 장치/게이트 이벤트 로그 저장
- 사진 분석 흐름과 별도 운영 모드로 연결

## 위치

- 실행 스크립트: `helmet_gate.py`
- 의존성: `requirements.txt`
- 초기 설치: `setup_helmet_gate.bat`
- 실행: `run_helmet_gate.bat`

## 빠른 실행

### 1. 초기 설치

`setup_helmet_gate.bat`

이 파일을 한 번 실행하면:

- `venv` 가상환경 생성
- 필요한 라이브러리 설치

까지 처리합니다.

### 2. 프로그램 실행

`run_helmet_gate.bat`

## 수동 실행 명령

```bat
cd /d C:\Users\4152\Desktop\DX\elkitoki-device\tools\helmet-gate
python -m venv venv
venv\Scripts\activate
pip install -r requirements.txt
python helmet_gate.py
```

## 카메라가 안 잡힐 때

`helmet_gate.py` 안의 이 줄을 바꿔서 다시 실행하면 됩니다.

```python
cap = cv2.VideoCapture(1)
```

필요하면 `0`, `1`, `2`로 바꿔가며 테스트합니다.

## 현재 엘키토키와의 관계

이 도구는 현재 **엘키토키 메인 서버와 분리된 독립 실행 앱**입니다.

즉:

- 엘키토키 웹/서버를 켜지 않아도 단독 실행 가능
- 시연 시에는 필요할 때만 따로 실행 가능

## 나중에 붙이는 가장 현실적인 방법

완전히 합치기보다는 아래 순서가 좋습니다.

1. `helmet_gate.py`는 계속 별도 실행
2. 헬멧 미착용 시 서버의 알림 API만 호출
3. 대시보드에 "헬멧 게이트 경고"를 별도 알림으로 표시

원하면 다음 단계로 이 연동까지 바로 추가할 수 있습니다.
