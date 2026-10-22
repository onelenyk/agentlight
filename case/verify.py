"""Перевірка корпусу перед друком. Запуск: .venv/bin/python case/verify.py

Перевіряє: цілісність сіток, що кожна деталь — одне тіло, нависання в положенні друку,
перетини у складеному стані, найменші зазори між сусідніми деталями і те, що все збирається рухом.
Код виходу 1, якщо знайдено проблему.
"""
import sys

import numpy as np

import build as B

BED = 180.0            # стіл A1 mini
MIN_GAP = 0.15         # менший зазор між рухомими деталями вважаємо ризиком
problems = []


def check(ok, text):
    print(("  ok   " if ok else "  FAIL ") + text)
    if not ok:
        problems.append(text)


def vol(a, b):
    return (a ^ b).volume()


P = B.parts()
board = B.dummy_board()
leds_list = B.dummy_leds()
leds = leds_list[0] + leds_list[1] + leds_list[2]
touch = B.dummy_touch()
LOCK = -24                                   # кут, на якому зуби сідають на полицю
dome_locked = P["dome"].rotate([0, 0, LOCK])

print("1. Цілісність сіток і положення на столі")
for name, m in P.items():
    posed = B.print_pose(name, m)
    tm = B.to_trimesh(posed)
    bb = posed.bounding_box()
    check(str(m.status()).endswith("NoError") and tm.is_watertight and tm.is_winding_consistent and tm.volume > 0,
          f"{name}: замкнена сітка без дірок, {len(tm.faces)} трикутників")
    check(len(m.decompose()) == 1, f"{name}: одне суцільне тіло")
    check(abs(bb[2]) < 1e-6 and max(bb[3], bb[4], bb[5]) < BED, f"{name}: стоїть на столі, {bb[3]:.1f} x {bb[4]:.1f} x {bb[5]:.1f} мм")
    bottom = tm.area_faces[(tm.face_normals[:, 2] < -0.999) & (tm.triangles_center[:, 2] < 0.01)].sum()
    check(bottom > 60, f"{name}: площа контакту зі столом {bottom:.0f} мм²")

print("\n2. Нависання в положенні друку (поверхні крутіші за 45°, не на столі)")
for name, m in P.items():
    tm = B.to_trimesh(B.print_pose(name, m))
    over = (tm.face_normals[:, 2] < -0.72) & (tm.triangles_center[:, 2] > 0.05) & (tm.area_faces > 1e-4)
    area = tm.area_faces[over].sum()
    if area < 0.5:
        check(True, f"{name}: нависань немає")
        continue
    c = tm.triangles[over].reshape(-1, 3)
    span = c.max(axis=0) - c.min(axis=0)
    # єдині допустимі нависання — спереду основи: стеля кишені під плату й верх вікна роз'єму, обидва — мости
    is_bridge = name == "base" and c[:, 0].min() > B.R_OUT + 8 and c[:, 2].max() <= B.Z_USB_MID + B.USB_H / 2 + B.USB_FIT + 0.01
    check(is_bridge, f"{name}: {area:.0f} мм² на висоті {c[:, 2].min():.1f}–{c[:, 2].max():.1f} мм, {span[0]:.1f} x {span[1]:.1f} мм"
          + (" — стеля кишені під плату й верх вікна USB, мости" if is_bridge else ""))

print("\n3. Перетини у складеному стані (мм³)")
solids = {**P, "dome": dome_locked, "плата": board, "діоди": leds, "сенсор": touch}
names = list(solids)
for i, a in enumerate(names):
    for b in names[i + 1:]:
        v = vol(solids[a], solids[b])
        if {a, b} == {"diffuser", "dome"}:
            check(20 < v < 80, f"{a} ∩ {b}: {v:.1f} — задуманий натяг {B.DIFF_PRELOAD} мм: ковпак притискає розсіювач до диска")
        elif v > 0.001:
            check(False, f"{a} ∩ {b}: {v:.3f}")
print("  ok   усі інші пари не перетинаються" if not any("∩" in p and "натяг" not in p for p in problems) else "")

print("\n4. Найменші зазори між сусідніми деталями (мм)")
# (деталь A, деталь B, очікування): contact — деталі мають торкатись; інакше мінімальний допустимий зазор
PAIRS = [
    ("base", "dome-open", MIN_GAP, "шийка і зуби в спідниці, ковпак надягнутий, але ще не повернутий"),
    ("base", "dome", "contact", "після повороту зуби лежать на полиці паза"),
    ("base", "carrier", "contact", "диск лежить на шийці"),
    ("carrier", "dome", "contact", "ковпак тисне на диск"),
    ("carrier", "diffuser", "contact", "розсіювач стоїть на диску"),
    ("diffuser", "dome", "contact", "ковпак тисне на стелю розсіювача"),
    ("base", "clamp", "contact", "засувка стоїть на дні"),
    ("clamp", "carrier", 0.1, "засувка трохи не дістає до диска"),
    ("clamp", "плата", "contact", "виступ засувки лежить на задньому краї плати"),
    ("base", "плата", "contact", "плата лежить на ребрі"),
    ("carrier", "діоди", MIN_GAP, "платки діодів у кишенях"),
    ("діоди", "diffuser", 1.0, "діоди до розсіювача"),
    ("base", "діоди", 1.0, "діоди до основи"),
    ("carrier", "сенсор", 1.0, "сенсорний модуль над кишенями: місце для дротів"),
    ("сенсор", "dome", B.TOUCH_SKIN - B.DIFF_PRELOAD - 0.01, "від модуля до ковпака: білий шар без повітря"),
]
for a, b, want, what in PAIRS:
    gap = solids[a].min_gap(P["dome"] if b == "dome-open" else solids[b], 5.0)
    if want == "contact":
        check(gap < 0.03, f"{a} — {b}: {gap:.2f}, {what}")
    else:
        check(gap >= want - 1e-6, f"{a} — {b}: {gap:.2f} (треба не менше {want}), {what}")

# Радіальні зазори, де деталь рухається відносно сусідньої (беремо з параметрів і перевіряємо геометрією)
r_gap_neck = B.R_SKIRT - B.R_NECK
r_gap_lug = B.R_SLOT - (B.R_NECK + B.LUG_OUT)
check(abs(r_gap_neck - B.FIT) < 1e-9 and r_gap_neck >= MIN_GAP, f"шийка в спідниці: {r_gap_neck:.2f} на радіус")
check(r_gap_lug >= 0.25, f"зуб у пазу: {r_gap_lug:.2f} на радіус")
check(B.R_SKIRT - (B.R_SKIRT - 0.3) >= 0.25, "диск у спідниці ковпака: 0.30 на радіус")
check(B.PCB_CLR >= 0.15, f"плата в гнізді: {B.PCB_CLR:.2f} з боків, {B.FRONT_GAP + B.CLAMP_GAP:.2f} по довжині")
check(B.USB_FIT <= 0.15, f"роз'єм у вікні: {B.USB_FIT:.2f} з боку — щільно")
corner = float(np.hypot(B.X_FRONT + B.FRONT_GAP, B.PCB_W / 2 + B.PCB_CLR))
check(B.R_OUT - corner >= 0.9, f"від кута кишені під плату до зовнішньої стінки: {B.R_OUT - corner:.2f}")
check(B.X_FLAT - (B.X_FRONT + B.FRONT_GAP) >= 1.2, f"стінка перед платою: {B.X_FLAT - B.X_FRONT - B.FRONT_GAP:.2f}")
check(B.LED_FIT >= 0.15, f"платка діода в кишені: {B.LED_FIT:.2f} на радіус, паз {B.LED_PCB_T + 0.3:.1f} мм під платку {B.LED_PCB_T} мм")
check(B.TOUCH_FIT >= 0.2, f"сенсорний модуль у гнізді: {B.TOUCH_FIT:.2f} з кожного боку, глибина {B.TOUCH_DEPTH} мм під модуль {B.TOUCH_T} мм")
check(B.TOUCH_SKIN + B.WALL_DOME <= 3.0, f"від модуля до пальця: {B.TOUCH_SKIN + B.WALL_DOME:.1f} мм (TTP223 чує приблизно до 5 мм)")
check(B.CLAMP_FIT >= 0.15, f"засувка в пазу: {B.CLAMP_FIT:.2f} з кожного боку")

print("\n5. Збирання: кожна деталь доходить до місця без зачіпань")


def sweep(moving, static, moves, label, allow=0.001):
    worst = max(vol(mv(moving), static) for mv in moves)
    check(worst <= allow, f"{label}: найбільший перетин на шляху {worst:.3f} мм³")


zs = np.arange(0, 22.01, 0.5)
# плата: опускається позаду і засувається вперед, роз'ємом у вікно
sweep(board, P["base"], [lambda m, z=z: m.translate([-B.SLIDE, 0, z]) for z in zs], "плата опускається позаду кишені")
sweep(board, P["base"], [lambda m, x=x: m.translate([-x, 0, 0]) for x in np.arange(0, B.SLIDE + 0.01, 0.2)],
      "плата засувається вперед, роз'єм заходить у вікно")
sweep(P["clamp"], P["base"] + board, [lambda m, z=z: m.translate([0, 0, z + 0.05]) for z in zs], "засувка опускається в пази")
check(vol(board.translate([-0.3, 0, 0]), P["clamp"]) > 0.05, "із засувкою плата назад не відсувається")
check(vol(board.translate([0.3, 0, 0]), P["base"]) > 0.05, "уперед плату тримає стінка")
sweep(leds, P["carrier"], [lambda m, z=z: m.translate([0, 0, z]) for z in zs], "платки діодів засуваються в кишені згори")
sweep(touch, P["diffuser"], [lambda m, z=z: m.translate([0, 0, -z]) for z in zs], "сенсорний модуль вставляється в гніздо знизу")
sweep(P["diffuser"] + touch, P["carrier"] + leds, [lambda m, z=z: m.translate([0, 0, z]) for z in zs], "розсіювач надягається на диск")
sweep(P["carrier"] + leds + P["diffuser"] + touch, P["base"] + P["clamp"], [lambda m, z=z: m.translate([0, 0, z]) for z in zs],
      "диск із діодами й розсіювачем лягає на шийку")
# розсіювач навмисно на DIFF_PRELOAD вищий, тому для руху ковпака беремо його без цього натягу
inner = P["base"] + P["carrier"] + P["diffuser"].translate([0, 0, -B.DIFF_PRELOAD - 0.01]) + leds + touch
sweep(P["dome"], inner, [lambda m, z=z: m.translate([0, 0, z]) for z in zs], "ковпак надягається (зуби у вхідних пазах)")
sweep(P["dome"], inner, [lambda m, a=a: m.rotate([0, 0, -a]) for a in np.arange(0, 24.01, 1.0)], "ковпак повертається до замикання")

print("\n6. Замок ковпака")
check(vol(dome_locked.translate([0, 0, 0.6]), P["base"]) > 0.5, "після повороту ковпак не знімається вгору")
check(vol(P["dome"].rotate([0, 0, 4]), P["base"]) > 0.5, "у зворотний бік ковпак не крутиться")
tight = next(a for a in np.arange(0, 40, 0.5) if vol(P["dome"].rotate([0, 0, -a]), P["base"]) > 0.01)
check(20 <= tight <= 30, f"полиця починає затягувати на {tight:.1f}° з {B.TWIST_DEG:.0f}° ходу: є запас в обидва боки")
for drop in (0.2, -0.2):   # ковпак надруковано на 0,2 мм вище чи нижче
    a = next((a for a in np.arange(0, 40, 0.5) if vol(P["dome"].translate([0, 0, drop]).rotate([0, 0, -a]), P["base"]) > 0.01), None)
    check(a is not None and 8 <= a <= B.TWIST_DEG, f"якщо висота ковпака відхилиться на {drop:+.1f} мм, затягне на {a}°")

print("\n" + ("УСЕ ГАРАЗД: проблем не знайдено" if not problems else f"ЗНАЙДЕНО ПРОБЛЕМ: {len(problems)}"))
for p in problems:
    print("  - " + p)
sys.exit(1 if problems else 0)
