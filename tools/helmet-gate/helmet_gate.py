import time
import cv2
import numpy as np
from ultralytics import YOLO
from huggingface_hub import hf_hub_download
from PIL import ImageFont, ImageDraw, Image
import winsound  # Windows 전용 소리

# =========================
# 설정값 (부스용)
# =========================
CONF_TH = 0.40                 # 오탐 많으면 0.40~0.50
WARN_BEEP_INTERVAL = 1.2       # 경고음 반복 간격(초)
ENTRY_HOLD_SEC = 3.0           # 입장 완료 화면 유지 시간(초)
FONT_PATH = "C:/Windows/Fonts/malgun.ttf"
WINDOW_NAME = "Safety Helmet Gate"
SITE_CODE = "#LGEDX-2026-92"

# 입장 판정 조건
REQUIRED_CENTER_HOLD_SEC = 2.0
GUIDE_BOX_WIDTH_RATIO = 0.48
GUIDE_TOP_Y_RATIO = 0.18
GUIDE_MIN_COVERAGE = 0.28
GUIDE_MIN_PERSON_TO_GUIDE_RATIO = 0.28
GUIDE_MAX_PERSON_TO_GUIDE_RATIO = 1.80
GUIDE_CENTER_TOL_X_RATIO = 0.30
GUIDE_CENTER_TOL_Y_RATIO = 0.30

# Helmet/no-helmet filtering (reduce cap false positives)
HELMET_CONF_TH = 0.55
NO_HELMET_CONF_TH = 0.45
HEAD_ITEM_MIN_INSIDE_RATIO = 0.80
HEAD_ITEM_CENTER_X_TOL_RATIO = 0.32
HEAD_ITEM_CENTER_Y_MAX_RATIO = 0.45
HEAD_ITEM_W_MIN_RATIO = 0.16
HEAD_ITEM_W_MAX_RATIO = 0.72
HEAD_ITEM_H_MIN_RATIO = 0.08
HEAD_ITEM_H_MAX_RATIO = 0.38

# 거울 모드(좌우 반전)
MIRROR_MODE = True

# 중앙 타겟 모드 UI
SHOW_SILHOUETTE_GUIDE = True
SHOW_TARGET_BOX = True

# =========================
# IOU + 유틸
# =========================
def iou(boxA, boxB):
    xA = max(boxA[0], boxB[0])
    yA = max(boxA[1], boxB[1])
    xB = min(boxA[2], boxB[2])
    yB = min(boxA[3], boxB[3])
    inter = max(0, xB - xA) * max(0, yB - yA)
    if inter == 0:
        return 0.0
    areaA = (boxA[2]-boxA[0])*(boxA[3]-boxA[1])
    areaB = (boxB[2]-boxB[0])*(boxB[3]-boxB[1])
    return inter / float(areaA + areaB - inter)

def box_center(box):
    x1, y1, x2, y2 = box
    return ((x1 + x2) / 2.0, (y1 + y2) / 2.0)

def dist2(ax, ay, bx, by):
    dx = ax - bx
    dy = ay - by
    return dx*dx + dy*dy

def box_area(box):
    x1, y1, x2, y2 = box
    return max(0, x2 - x1) * max(0, y2 - y1)

def intersection_area(boxA, boxB):
    xA = max(boxA[0], boxB[0])
    yA = max(boxA[1], boxB[1])
    xB = min(boxA[2], boxB[2])
    yB = min(boxA[3], boxB[3])
    return max(0, xB - xA) * max(0, yB - yA)

def head_item_matches_person(person_box, item_box):
    px1, py1, px2, py2 = person_box
    ix1, iy1, ix2, iy2 = item_box

    pw = max(1, px2 - px1)
    ph = max(1, py2 - py1)
    iw = max(1, ix2 - ix1)
    ih = max(1, iy2 - iy1)
    ia = iw * ih

    inside_ratio = intersection_area(item_box, person_box) / float(ia)

    pcx, _ = box_center(person_box)
    icx, icy = box_center(item_box)
    x_ok = abs(icx - pcx) <= (pw * HEAD_ITEM_CENTER_X_TOL_RATIO)
    y_ok = (py1 + ph * 0.02) <= icy <= (py1 + ph * HEAD_ITEM_CENTER_Y_MAX_RATIO)

    w_ratio = iw / float(pw)
    h_ratio = ih / float(ph)
    size_ok = (
        HEAD_ITEM_W_MIN_RATIO <= w_ratio <= HEAD_ITEM_W_MAX_RATIO
        and HEAD_ITEM_H_MIN_RATIO <= h_ratio <= HEAD_ITEM_H_MAX_RATIO
    )

    return inside_ratio >= HEAD_ITEM_MIN_INSIDE_RATIO and x_ok and y_ok and size_ok

def build_guide_box(frame_w, frame_h):
    gw = int(frame_w * GUIDE_BOX_WIDTH_RATIO)
    cx = frame_w // 2
    x1 = max(0, cx - gw // 2)
    y1 = max(120, int(frame_h * GUIDE_TOP_Y_RATIO))  # 배너 아래에서 시작
    x2 = min(frame_w - 1, x1 + gw)
    y2 = frame_h - 1
    return (x1, y1, x2, y2)

def person_matches_guide(person_box, guide_box):
    g_area = box_area(guide_box)
    p_area = box_area(person_box)
    if g_area <= 0:
        return False, {"coverage": 0.0, "size_ratio": 0.0, "coverage_ok": False, "size_ok": False, "center_ok": False}

    cov = intersection_area(person_box, guide_box) / float(g_area)
    size_ratio = p_area / float(g_area)

    pcx, pcy = box_center(person_box)
    gcx, gcy = box_center(guide_box)
    gw = max(1, guide_box[2] - guide_box[0])
    gh = max(1, guide_box[3] - guide_box[1])
    x_ok = abs(pcx - gcx) <= (gw * GUIDE_CENTER_TOL_X_RATIO)
    y_ok = abs(pcy - gcy) <= (gh * GUIDE_CENTER_TOL_Y_RATIO)
    center_ok = x_ok and y_ok
    coverage_ok = cov >= GUIDE_MIN_COVERAGE
    size_ok = GUIDE_MIN_PERSON_TO_GUIDE_RATIO <= size_ratio <= GUIDE_MAX_PERSON_TO_GUIDE_RATIO

    matched = center_ok and coverage_ok and size_ok
    return matched, {
        "coverage": cov,
        "size_ratio": size_ratio,
        "coverage_ok": coverage_ok,
        "size_ok": size_ok,
        "center_ok": center_ok,
    }

def draw_silhouette_guide(frame, guide_box, matched=False):
    x1, y1, x2, y2 = guide_box
    gw = max(1, x2 - x1)
    gh = max(1, y2 - y1)
    cx = x1 + gw // 2
    color = (255, 255, 255)
    thickness = max(5, int(min(gw, gh) * 0.020))

    # Head: near-circle (sample icon style)
    head_r = max(24, int(gw * 0.20))
    head_center = (cx, y1 + head_r + int(gh * 0.02))
    cv2.ellipse(frame, head_center, (head_r, head_r), 0, 0, 360, color, thickness, cv2.LINE_AA)

    # Rounded shoulders
    shoulder_axes = (int(gw * 0.27), int(gh * 0.12))
    shoulder_top_y = head_center[1] + head_r + int(gh * 0.03)
    shoulder_center = (cx, shoulder_top_y + shoulder_axes[1])
    cv2.ellipse(frame, shoulder_center, shoulder_axes, 0, 180, 360, color, thickness, cv2.LINE_AA)

    # Body: slanted sides to the bottom of screen
    left_shoulder = (cx - shoulder_axes[0], shoulder_center[1])
    right_shoulder = (cx + shoulder_axes[0], shoulder_center[1])
    left_base = (x1 + int(gw * 0.08), y2)
    right_base = (x2 - int(gw * 0.08), y2)
    cv2.line(frame, left_shoulder, left_base, color, thickness, cv2.LINE_AA)
    cv2.line(frame, right_shoulder, right_base, color, thickness, cv2.LINE_AA)
    cv2.line(frame, left_base, right_base, color, thickness, cv2.LINE_AA)

    return frame

# =========================
# 한글 텍스트 (PIL)
# =========================
def put_korean_text(img_bgr, text, xy, font_size=46, color_rgb=(255, 255, 255)):
    font = ImageFont.truetype(FONT_PATH, font_size)
    img_rgb = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2RGB)
    pil_img = Image.fromarray(img_rgb)
    draw = ImageDraw.Draw(pil_img)
    draw.text(xy, text, font=font, fill=color_rgb)
    out = cv2.cvtColor(np.array(pil_img), cv2.COLOR_RGB2BGR)
    return out

# =========================
# 사운드
# =========================
def play_ok_sound():
    winsound.Beep(1200, 150)
    winsound.Beep(1600, 120)

def play_warning_sound():
    return

# =========================
# UI: 상단 배너 + 아이콘
# =========================
def draw_top_banner(frame, text, ok, pulse_t):
    h, w = frame.shape[:2]
    banner_color = (0, 200, 0) if ok else (0, 0, 255)
    cv2.rectangle(frame, (0, 0), (w, 120), banner_color, -1)
    frame = put_korean_text(frame, text, (30, 30), font_size=52, color_rgb=(255, 255, 255))

    icon_cx, icon_cy = w - 90, 60
    if ok:
        radius = int(30 + 6 * np.sin(pulse_t * 2 * np.pi))
        cv2.circle(frame, (icon_cx, icon_cy), radius, (255, 255, 255), 3)
        cv2.line(frame, (icon_cx - 18, icon_cy + 2), (icon_cx - 4, icon_cy + 16), (255, 255, 255), 6)
        cv2.line(frame, (icon_cx - 4, icon_cy + 16), (icon_cx + 22, icon_cy - 12), (255, 255, 255), 6)
    else:
        cv2.circle(frame, (icon_cx, icon_cy), 30, (255, 255, 255), 3)
        cv2.line(frame, (icon_cx, icon_cy - 15), (icon_cx, icon_cy + 8), (255, 255, 255), 6)
        cv2.circle(frame, (icon_cx, icon_cy + 18), 4, (255, 255, 255), -1)

    return frame

def draw_site_code(frame, site_code):
    h, w = frame.shape[:2]
    font = cv2.FONT_HERSHEY_SIMPLEX
    scale = 0.95
    thick = 2
    margin = 24

    (tw, th), baseline = cv2.getTextSize(site_code, font, scale, thick)
    x = w - tw - margin
    y = h - margin

    bx1 = x - 14
    by1 = y - th - 14
    bx2 = x + tw + 14
    by2 = y + baseline + 10

    overlay = frame.copy()
    cv2.rectangle(overlay, (bx1, by1), (bx2, by2), (0, 0, 0), -1)
    frame = cv2.addWeighted(overlay, 0.42, frame, 0.58, 0)
    cv2.putText(frame, site_code, (x, y), font, scale, (255, 255, 255), thick, cv2.LINE_AA)
    return frame

# =========================
# 화면 오버레이: 입장 완료 / 출입 불가
# =========================
def overlay_fullscreen_message(frame, title, subtitle, ok=True):
    h, w = frame.shape[:2]
    overlay = frame.copy()
    base_color = (0, 170, 0) if ok else (0, 0, 220)
    cv2.rectangle(overlay, (0, 0), (w, h), base_color, -1)
    frame = cv2.addWeighted(overlay, 0.55, frame, 0.45, 0)

    card_w, card_h = int(w * 0.75), int(h * 0.45)
    x1 = (w - card_w) // 2
    y1 = (h - card_h) // 2
    x2 = x1 + card_w
    y2 = y1 + card_h
    cv2.rectangle(frame, (x1, y1), (x2, y2), (255, 255, 255), -1)
    cv2.rectangle(frame, (x1, y1), (x2, y2), (40, 40, 40), 3)

    frame = put_korean_text(frame, title, (x1 + 60, y1 + 80), font_size=80, color_rgb=(0, 0, 0))
    frame = put_korean_text(frame, subtitle, (x1 + 60, y1 + 200), font_size=48, color_rgb=(30, 30, 30))

    cx, cy = x2 - 130, y1 + 140
    if ok:
        cv2.circle(frame, (cx, cy), 55, (0, 180, 0), 8)
        cv2.line(frame, (cx - 35, cy + 5), (cx - 10, cy + 30), (0, 180, 0), 10)
        cv2.line(frame, (cx - 10, cy + 30), (cx + 45, cy - 25), (0, 180, 0), 10)
    else:
        cv2.circle(frame, (cx, cy), 55, (0, 0, 220), 8)
        cv2.line(frame, (cx - 30, cy - 30), (cx + 30, cy + 30), (0, 0, 220), 10)
        cv2.line(frame, (cx + 30, cy - 30), (cx - 30, cy + 30), (0, 0, 220), 10)

    return frame

# =========================
# ✅ 마스크 제거: person/helmet/no-helmet만 "표시"하기
# =========================
def is_allowed_label(name: str) -> bool:
    n = name.lower()
    return ("person" in n) or ("hardhat" in n) or ("no-hardhat" in n) or ("no hardhat" in n) or ("nohardhat" in n)

def draw_filtered_boxes(frame, r, target_person=None):
    if r.boxes is None or len(r.boxes) == 0:
        return frame

    names = r.names
    for box in r.boxes:
        cls_id = int(box.cls[0].item())
        label = names.get(cls_id, str(cls_id))
        if not is_allowed_label(label):
            continue  # ✅ 마스크 등은 화면에 표시 안 함

        x1, y1, x2, y2 = map(int, box.xyxy[0].tolist())
        l = label.lower()

        # 색상
        if "person" in l:
            color = (255, 255, 255)
            short = "person"
        elif "no" in l and "hardhat" in l:
            color = (0, 0, 255)
            short = "no-helmet"
        else:
            color = (0, 255, 0)
            short = "helmet"

        cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)
        cv2.putText(frame, short, (x1, max(20, y1 - 8)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, color, 2, cv2.LINE_AA)

    # 타겟 강조(노란 박스)
    if target_person is not None and SHOW_TARGET_BOX:
        x1, y1, x2, y2 = target_person
        cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 255, 255), 4)

    return frame

# =========================
# 모델 로드
# =========================
print("### RUNNING: CENTER-TARGET + MIRROR + NO-MASK DISPLAY ###")

ckpt_path = hf_hub_download(
    repo_id="Hansung-Cho/yolov8-ppe-detection",
    filename="best.pt"
)
model = YOLO(ckpt_path)

# =========================
# 카메라 설정
# =========================
cap = cv2.VideoCapture(1)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

# =========================
# 창: 전체 화면
# =========================
cv2.namedWindow(WINDOW_NAME, cv2.WND_PROP_FULLSCREEN)
cv2.setWindowProperty(WINDOW_NAME, cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN)

# =========================
# 상태 제어
# =========================
prev_state = None
last_warn_beep = 0
start_time = time.time()
center_hold_start = None

gate_hold_until = 0
last_ok_frame = None

print("실행 중... 종료: 화면 클릭 후 q")

while cap.isOpened():
    success, frame = cap.read()
    if not success:
        break

    # ✅ 거울 모드
    if MIRROR_MODE:
        frame = cv2.flip(frame, 1)

    now = time.time()

    # [A] 입장 완료 홀드
    if now < gate_hold_until and last_ok_frame is not None:
        hold_view = overlay_fullscreen_message(
            last_ok_frame.copy(),
            title="입장 완료",
            subtitle="안전모 확인되었습니다. 들어오세요!",
            ok=True
        )
        hold_view = draw_site_code(hold_view, SITE_CODE)
        cv2.imshow(WINDOW_NAME, hold_view)
        if cv2.waitKey(1) & 0xFF == ord("q"):
            break
        continue

    # [B] 추론
    results = model(frame, verbose=False, conf=CONF_TH)
    r = results[0]
    names = r.names

    person_boxes = []
    helmet_boxes = []
    nohelmet_boxes = []

    if r.boxes is not None and len(r.boxes) > 0:
        for box in r.boxes:
            cls_id = int(box.cls[0].item())
            cls_name = names.get(cls_id, str(cls_id)).lower()
            x1, y1, x2, y2 = map(int, box.xyxy[0].tolist())
            conf = float(box.conf[0].item()) if box.conf is not None else 0.0

            if "person" in cls_name:
                person_boxes.append((x1, y1, x2, y2))
            elif "hardhat" in cls_name and "no" not in cls_name:
                if conf >= HELMET_CONF_TH:
                    helmet_boxes.append((x1, y1, x2, y2))
            elif "no-hardhat" in cls_name or "no hardhat" in cls_name or "nohardhat" in cls_name:
                if conf >= NO_HELMET_CONF_TH:
                    nohelmet_boxes.append((x1, y1, x2, y2))

    # ✅ 중앙 타겟: 화면 중앙에 가장 가까운 사람 1명만 판정
    h, w = frame.shape[:2]
    center_x, center_y = w / 2.0, h / 2.0
    guide_box = build_guide_box(w, h)

    target_person = None
    person_count = len(person_boxes)
    if person_count > 0:
        target_person = min(
            person_boxes,
            key=lambda p: dist2(box_center(p)[0], box_center(p)[1], center_x, center_y)
        )

    # 타겟 사람 기준으로만 헬멧 판정
    helmet_cnt = 0
    no_helmet_cnt = 0
    if target_person is not None:
        for hb in helmet_boxes:
            if head_item_matches_person(target_person, hb):
                helmet_cnt += 1
        for nhb in nohelmet_boxes:
            if head_item_matches_person(target_person, nhb):
                no_helmet_cnt += 1

    guide_fit_ok = False
    guide_fit_info = None
    if target_person is not None:
        guide_fit_ok, guide_fit_info = person_matches_guide(target_person, guide_box)

    ready_to_hold = (
        (target_person is not None)
        and (helmet_cnt > 0)
        and (no_helmet_cnt == 0)
        and guide_fit_ok
    )

    if ready_to_hold:
        if center_hold_start is None:
            center_hold_start = now
        hold_elapsed = now - center_hold_start
    else:
        center_hold_start = None
        hold_elapsed = 0.0

    # 상태 결정
    if person_count == 0:
        state = "IDLE"
        status_text = "카메라를 바라봐 주세요"
        ok = False
    elif no_helmet_cnt > 0:
        center_hold_start = None
        state = "WARN"
        status_text = "안전모를 착용해주세요"
        ok = False
    elif target_person is not None and not guide_fit_ok:
        center_hold_start = None
        state = "ALIGN"
        if guide_fit_info is not None and not guide_fit_info["coverage_ok"]:
            status_text = "실루엣 가이드 안으로 들어와 주세요"
        elif guide_fit_info is not None and not guide_fit_info["size_ok"]:
            status_text = "가이드 크기에 맞게 거리 조절해주세요"
        else:
            status_text = "실루엣 중심선에 맞춰 서주세요"
        ok = False
    elif ready_to_hold and hold_elapsed >= REQUIRED_CENTER_HOLD_SEC:
        state = "OK"
        status_text = "입장 가능합니다"
        ok = True
    elif ready_to_hold:
        state = "HOLD"
        remain = max(0.0, REQUIRED_CENTER_HOLD_SEC - hold_elapsed)
        status_text = f"가이드 위치에서 {remain:.1f}초 유지해주세요"
        ok = False
    else:
        state = "IDLE"
        status_text = "카메라를 바라봐 주세요"
        ok = False

    # ✅ r.plot() 대신 직접 그리기(마스크 표시 제거)
    annotated = frame.copy()
    annotated = draw_filtered_boxes(annotated, r, target_person=target_person)

    # 실루엣 가이드(머리/가슴선)
    if SHOW_SILHOUETTE_GUIDE:
        annotated = draw_silhouette_guide(annotated, guide_box, matched=guide_fit_ok)

    elapsed = time.time() - start_time
    pulse_t = (elapsed % 1.0)
    annotated = draw_top_banner(annotated, status_text, ok, pulse_t)
    annotated = draw_site_code(annotated, SITE_CODE)

    # 사운드 + 홀드
    if state != prev_state:
        if state == "OK":
            play_ok_sound()
            last_ok_frame = annotated.copy()
            gate_hold_until = now + ENTRY_HOLD_SEC
        elif state == "WARN":
            play_warning_sound()
            last_warn_beep = now
        prev_state = state
    else:
        if state == "WARN" and (now - last_warn_beep) >= WARN_BEEP_INTERVAL:
            play_warning_sound()
            last_warn_beep = now

    # WARN이면 출입 불가 오버레이
    # if state == "WARN":
    #     annotated = overlay_fullscreen_message(
    #         annotated,
    #         title="출입 불가",
    #         subtitle="안전모 착용 후 다시 시도하세요",
    #         ok=False
    #     )

    cv2.imshow(WINDOW_NAME, annotated)

    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cap.release()
cv2.destroyAllWindows()
