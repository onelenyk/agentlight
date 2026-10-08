"""Корпус AgentLight: ліхтарик Ø38 x 36 мм під ESP32-C3 Super Mini і 3 x WS2812B на круглих платках Ø9,5.

П'ять деталей, усі друкуються без підтримок:
  base    — основа з тунелем під USB-C і шийкою з трьома зубами байонета
  clamp   — рамка, що притискає плату за корпус USB-роз'єму
  carrier — диск із трьома кишенями: платки зі світлодіодами засуваються в них згори
  diffuser — білий тонкий стакан навколо діодів: розсіює світло і ховає нутрощі
  dome    — зовнішній ковпак (прозорий), надягається і закручується за годинниковою стрілкою

Запуск:  .venv/bin/python case/build.py   ->  case/stl/*.stl (уже в положенні для друку)
Усі розміри в мм. Координати складання: Z угору, USB дивиться в +X.
"""
import pathlib

import numpy as np
import trimesh
from manifold3d import CrossSection, Manifold, OpType, set_circular_segments

set_circular_segments(160)

# ---- Плата: типові розміри Super Mini, ті самі, що в meteor-orb/cad/orb.py (товщину варто поміряти) ----
PCB_L, PCB_W, PCB_T = 22.52, 18.0, 1.0
USB_W, USB_H, USB_LEN, USB_OVERHANG = 8.94, 3.2, 7.3, 1.2   # роз'єм: ширина, висота, довжина, виступ за край плати
PCB_CLR = 0.2          # зазор навколо плати
PCB_RAISE = 1.5        # просвіт під платою для кінчиків дротів і крапель припою

# ---- Корпус ----
R_OUT = 19.0           # зовнішній радіус
R_IN = 15.2            # внутрішній радіус основи
FLOOR = 1.6
Z_SHOULDER = 12.0      # верх чорної частини основи
Z_NECK = 17.0          # верх шийки
R_NECK = 16.6          # зовнішній радіус шийки
FIT = 0.2              # зазор між шийкою і спідницею ковпака
SEAM = 0.3             # видима щілина між основою і ковпаком (ковпак тисне на диск, а не на основу)
TUNNEL_W, TUNNEL_H = 14.0, 8.0   # отвір під штекер USB-C разом з його пластиковим корпусом

# ---- Світлодіоди: круглі платки Ø9,5, з діодом 2,7 мм завтовшки (поміряно для meteor-orb), контакти ззаду ----
LED_PCB_D = 9.5
LED_PCB_T = 1.2        # сама платка без діода (оцінка: 2,7 мм мінус корпус 5050)
LED_FIT = 0.2          # зазор платки в кишені з кожного боку
LED_WINDOW = 8.0       # виріз спереду під діод, як вікно Ø8 в meteor-orb
LED_BACK = 7.4         # виріз ззаду під контакти й дроти
LED_LIP = 0.9          # товщина передньої і задньої губ кишені
HOLDER_W = 13.6        # ширина однієї кишені; сусідні зрощуються кутами в спільні стійки
HOLDER_R0 = 3.6        # відстань від осі до задньої стінки кишені; всередині трикутний колодязь для дротів
FLANGE_T = 1.2
WALL_DOME = 1.2        # стінка розсіювача: тонша — яскравіше, але видно цятки
TOP_GAP = 5.0          # від верху кишень до стелі ковпака: тут ще стеля розсіювача і повітря над нею
DIFF_R = 15.0          # зовнішній радіус розсіювача; до стінки ковпака лишається ~2,8 мм повітря
DIFF_T = 0.8           # стінка розсіювача: два периметри білого
DIFF_RIM_H = 1.5       # бортик на диску, на який розсіювач сідає з легким натягом

# Сенсорна кнопка TTP223 (червоний модуль 15 x 11 мм) у стелі розсіювача
TOUCH_L, TOUCH_W, TOUCH_T = 15.0, 11.0, 2.0   # типові розміри модуля з деталями, не виміряні
TOUCH_FIT = 0.25       # зазор модуля в гнізді з кожного боку
TOUCH_DEPTH = 2.4      # глибина гнізда: модуль ховається повністю
TOUCH_SKIN = 0.8       # білий шар між модулем і ковпаком
DIFF_PRELOAD = 0.1     # розсіювач на стільки вищий за місце під ковпаком: ковпак притискає його до диска, і він не бовтається

# ---- Байонет ----
N_LUGS = 3
LUG_DEG = 14.0         # кутова ширина зуба
LUG_OUT = 1.1          # виступ зуба
LUG_TOP = 16.0
LUG_SIDE = 1.6         # висота вертикальної частини зуба, нижче — фаска 45° (друкується без підтримки)
TWIST_DEG = 35.0       # хід повороту
CAM_START, CAM_RISE = -0.6, 0.9   # полиця паза піднімається по ходу: спершу люфт 0,6, наприкінці натяг 0,3

# ---- Похідні ----
X_FRONT = 11.9                              # передній край плати впирається сюди
X_REAR = X_FRONT - PCB_L - 0.3              # задній упор
Z_PCB = FLOOR + PCB_RAISE                   # низ плати
Z_PCB_TOP = Z_PCB + PCB_T
Z_USB_TOP = Z_PCB_TOP + USB_H
Z_USB_MID = Z_PCB_TOP + USB_H / 2
Z_POCKET = Z_PCB_TOP + 0.7                  # до цієї висоти порожнина зрізана спереду під упор плати
R_SKIRT = R_NECK + FIT                      # внутрішній радіус спідниці ковпака
R_SLOT = R_NECK + LUG_OUT + 0.3             # дно паза байонета
Z_FLANGE_TOP = Z_NECK + FLANGE_T
LED_SEAT_R = LED_PCB_D / 2 + LED_FIT
Z_LED = Z_FLANGE_TOP + 1.2 + LED_SEAT_R       # центр платки
Z_POST_TOP = Z_LED + LED_SEAT_R + 0.6
HOLDER_R1 = HOLDER_R0 + 2 * LED_LIP + LED_PCB_T + 0.3   # передня площина кишені
Z_CEIL = Z_POST_TOP + TOP_GAP
HEIGHT = Z_CEIL + WALL_DOME
R_DOME_IN = R_OUT - WALL_DOME
CLAMP_X0, CLAMP_T = 5.85, 2.5               # рамка стоїть над USB-роз'ємом
CLAMP_FIT = 0.2                             # зазор ніжки рамки в пазу, з кожного боку
CLAMP_SHORT = 0.15                          # рамка нижча за шийку: диск має лягти на шийку, а не на рамку
CLAMP_Y_IN, CLAMP_Y_OUT = PCB_W / 2 + 0.4, 11.7


def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def cyl(r, z0, z1):
    return Manifold.cylinder(z1 - z0, r, r).translate([0, 0, z0])


def revolve(profile, deg=360.0, start=0.0):
    """Обертає профіль [(r, z), ...] навколо осі Z на deg градусів, починаючи з кута start."""
    pts = np.array(profile, dtype=float)
    area = 0.5 * np.sum(pts[:, 0] * np.roll(pts[:, 1], -1) - np.roll(pts[:, 0], -1) * pts[:, 1])
    if area < 0:
        pts = pts[::-1]
    segs = 0 if deg >= 360 else max(3, int(deg / 2))
    return Manifold.revolve(CrossSection([pts.tolist()]), segs, deg).rotate([0, 0, start])


def lug_angles():
    return [60 + i * 360 / N_LUGS for i in range(N_LUGS)]


def lug_bottom(r):
    """Нижня (скошена) поверхня зуба: 45° назовні-вгору."""
    return LUG_TOP - LUG_SIDE - LUG_OUT + (r - R_NECK)


def make_base():
    shell = revolve([(0, 0), (R_OUT - 0.8, 0), (R_OUT, 0.8), (R_OUT, Z_SHOULDER), (R_NECK, Z_SHOULDER),
                     (R_NECK, Z_NECK), (R_IN, Z_NECK), (R_IN, FLOOR), (0, FLOOR)])
    inside = cyl(R_IN + 0.5, FLOOR - 0.01, Z_NECK)   # усе, що додаємо всередину, обрізаємо цим (з перекриттям у стінку)

    parts = [
        box(X_FRONT, 20, -20, 20, FLOOR, Z_POCKET),                        # передній упор плати
        box(X_REAR - 1, X_FRONT, -4, 4, FLOOR, Z_PCB),                         # опорне ребро по центру, між рядами пінів
        box(-20, X_REAR, -5, 5, FLOOR, Z_PCB_TOP + 1.3),                   # задній упор
    ]
    # Гачок на задньому упорі: плата заводиться під нього заднім краєм
    hook = CrossSection([[(X_REAR - 0.5, Z_PCB_TOP - 0.5), (X_REAR + 0.8, Z_PCB_TOP + 0.8),
                          (X_REAR + 0.8, Z_PCB_TOP + 1.3), (X_REAR - 0.5, Z_PCB_TOP + 1.3)]])
    parts.append(Manifold.extrude(hook, 9.6).rotate([90, 0, 0]).translate([0, 4.8, 0]))
    side = PCB_W / 2 + PCB_CLR
    for sy in (1, -1):
        y0, y1 = sorted((sy * side, sy * 20))
        parts.append(box(-9, -6, y0, y1, FLOOR, Z_POCKET))                 # бічні напрямні ззаду
        # Пара ребер: напрямні плати і паз для ніжки рамки між ними
        parts.append(box(CLAMP_X0 - CLAMP_FIT - 1.8, CLAMP_X0 - CLAMP_FIT, y0, y1, FLOOR, 8.0))
        parts.append(box(CLAMP_X0 + CLAMP_T + CLAMP_FIT, CLAMP_X0 + CLAMP_T + CLAMP_FIT + 1.8, y0, y1, FLOOR, 8.0))
    base = shell + (Manifold.batch_boolean(parts, OpType.Add) ^ inside)

    for a in lug_angles():
        base += revolve([(R_NECK - 0.2, lug_bottom(R_NECK) - 0.2), (R_NECK, lug_bottom(R_NECK)),
                         (R_NECK + LUG_OUT, LUG_TOP - LUG_SIDE), (R_NECK + LUG_OUT, LUG_TOP),
                         (R_NECK - 0.2, LUG_TOP)], LUG_DEG, a - LUG_DEG / 2)
    for a in (90, 270):  # шипи на шийці: не дають диску провертатись разом з ковпаком
        base += revolve([(R_IN + 0.15, Z_NECK - 0.2), (R_NECK - 0.15, Z_NECK - 0.2), (R_NECK - 0.15, Z_NECK + 1.0),
                         (R_IN + 0.15, Z_NECK + 1.0)], 10, a - 5)
    tunnel = box(X_FRONT - 2, 30, -TUNNEL_W / 2, TUNNEL_W / 2, Z_USB_MID - TUNNEL_H / 2, Z_USB_MID + TUNNEL_H / 2)
    return base - tunnel


def make_clamp():
    """Рамка в площині YZ: ніжки стоять на дні, нижня перекладина тисне на USB-роз'єм, верхня підпирає диск."""
    z0, z1 = FLOOR, Z_NECK - CLAMP_SHORT
    beam0 = Z_USB_TOP - 0.15              # легкий натяг; перекладина тонка і пружинить
    beam1 = beam0 + 1.4
    outer = box(CLAMP_X0, CLAMP_X0 + CLAMP_T, -CLAMP_Y_OUT, CLAMP_Y_OUT, z0, z1)
    lower = box(0, 20, -CLAMP_Y_IN, CLAMP_Y_IN, z0 - 1, beam0)
    window = box(0, 20, -CLAMP_Y_IN, CLAMP_Y_IN, beam1, z1 - 2.5)
    return outer - lower - window


def slot(radius, x0, x1):
    """Виріз уздовж радіуса (+X): круг навколо центру платки плюс прямий хід угору — платка засувається згори."""
    round_part = Manifold.cylinder(x1 - x0, radius, radius).rotate([0, 90, 0]).translate([x0, 0, Z_LED])
    return round_part + box(x0, x1, -radius, radius, Z_LED, Z_POST_TOP + 1)


def make_holder():
    """Кишеня під одну платку: дивиться в +X. Платку тримають губи спереду і ззаду, діод виглядає у передній виріз."""
    groove0 = HOLDER_R0 + LED_LIP
    groove1 = HOLDER_R1 - LED_LIP
    body = box(HOLDER_R0, HOLDER_R1, -HOLDER_W / 2, HOLDER_W / 2, Z_FLANGE_TOP - 0.2, Z_POST_TOP)
    return (body - slot(LED_SEAT_R, groove0, groove1)
            - slot(LED_WINDOW / 2, groove1 - 0.1, HOLDER_R1 + 1)
            - slot(LED_BACK / 2, HOLDER_R0 - 1, groove0 + 0.1))


def led_angles():
    return (60, 180, 300)


def make_carrier():
    flange = cyl(R_SKIRT - 0.3, Z_NECK, Z_FLANGE_TOP)
    for a in (90, 270):
        flange -= revolve([(R_IN - 0.3, Z_NECK - 1), (R_OUT, Z_NECK - 1), (R_OUT, Z_FLANGE_TOP + 1),
                           (R_IN - 0.3, Z_FLANGE_TOP + 1)], 13, a - 6.5)
    flange -= cyl(HOLDER_R0 - 0.4, Z_NECK - 1, Z_FLANGE_TOP + 1)     # дроти йдуть униз крізь колодязь по центру
    holder = make_holder()
    r_rim = DIFF_R - DIFF_T - 0.1
    flange += revolve([(r_rim - 0.8, Z_FLANGE_TOP - 0.1), (r_rim, Z_FLANGE_TOP - 0.1),
                       (r_rim, Z_FLANGE_TOP + DIFF_RIM_H - 0.4), (r_rim - 0.4, Z_FLANGE_TOP + DIFF_RIM_H),
                       (r_rim - 0.8, Z_FLANGE_TOP + DIFF_RIM_H)])     # фаска зверху: розсіювач легше надягати
    return flange + Manifold.batch_boolean([holder.rotate([0, 0, a]) for a in led_angles()], OpType.Add)


def dummy_leds():
    """Платки з діодами в кишенях — для перевірки зазорів і для превью."""
    x0 = HOLDER_R0 + LED_LIP + 0.15
    pcb = Manifold.cylinder(LED_PCB_T, LED_PCB_D / 2, LED_PCB_D / 2).rotate([0, 90, 0]).translate([x0, 0, Z_LED])
    chip = box(x0 + LED_PCB_T, x0 + 2.7, -2.5, 2.5, Z_LED - 2.5, Z_LED + 2.5)
    return [(pcb + chip).rotate([0, 0, a]) for a in led_angles()]


def make_diffuser():
    """Білий стакан догори дном: стоїть на диску навколо кишень. Товста стеля сягає ковпака, у ній знизу гніздо
    під сенсорний модуль: крізь товстий білий шар модуль не видно, а до пальця лишається 2 мм."""
    z0, zt, ri, cham = Z_FLANGE_TOP, Z_CEIL + DIFF_PRELOAD, DIFF_R - DIFF_T, 1.5
    zr = zt - TOUCH_SKIN - TOUCH_DEPTH                      # низ стелі
    cup = revolve([(DIFF_R, z0), (DIFF_R, zt - cham), (DIFF_R - cham, zt), (0, zt), (0, zr), (ri, zr), (ri, z0)])
    return cup - touch_box(TOUCH_FIT, zr - 1, zt - TOUCH_SKIN)


def touch_box(fit, z0, z1):
    return box(-TOUCH_L / 2 - fit, TOUCH_L / 2 + fit, -TOUCH_W / 2 - fit, TOUCH_W / 2 + fit, z0, z1)


def dummy_touch():
    """Сенсорний модуль у гнізді — для перевірки зазорів і для превью."""
    zt = Z_CEIL + DIFF_PRELOAD - TOUCH_SKIN
    return touch_box(0, zt - TOUCH_T, zt)


def make_dome():
    z_rim = Z_SHOULDER + SEAM
    cham = 2.5
    dome = revolve([(0, HEIGHT), (R_OUT - cham, HEIGHT), (R_OUT, HEIGHT - cham), (R_OUT, z_rim), (R_SKIRT, z_rim),
                    (R_SKIRT, Z_FLANGE_TOP), (R_IN + 0.2, Z_FLANGE_TOP),          # кільце, що притискає диск
                    (R_DOME_IN, Z_FLANGE_TOP + R_DOME_IN - R_IN - 0.2),           # ...і скіс 45° над ним
                    (R_DOME_IN, Z_CEIL - 2.0), (R_DOME_IN - 2.0, Z_CEIL), (0, Z_CEIL)])
    z_top = LUG_TOP + 0.3
    step = 1.75
    for a in lug_angles():
        # Вертикальний вхід для зуба
        dome -= revolve([(R_SKIRT - 0.1, z_rim - 1), (R_SLOT, z_rim - 1), (R_SLOT, z_top), (R_SKIRT - 0.1, z_top)],
                        LUG_DEG + 4, a - LUG_DEG / 2 - 2)
        # Горизонтальний хід; полиця під зубом скошена на 45° і плавно піднімається — затягує ковпак донизу
        start = a + LUG_DEG / 2
        for k in range(int(TWIST_DEG / step) + 1):
            f = CAM_START + CAM_RISE * min(1.0, k * step / TWIST_DEG)
            dome -= revolve([(R_SKIRT - 0.1, lug_bottom(R_SKIRT - 0.1) + f), (R_SLOT, lug_bottom(R_SLOT) + f),
                             (R_SLOT, z_top), (R_SKIRT - 0.1, z_top)], step + 0.05, start + k * step)
    return dome


def dummy_board():
    """Плата з роз'ємом — лише для перевірки зазорів."""
    pcb = box(X_FRONT - PCB_L, X_FRONT, -PCB_W / 2, PCB_W / 2, Z_PCB, Z_PCB_TOP)
    usb = box(X_FRONT + USB_OVERHANG - USB_LEN, X_FRONT + USB_OVERHANG, -USB_W / 2, USB_W / 2, Z_PCB_TOP, Z_USB_TOP)
    return pcb + usb


def to_trimesh(m):
    mesh = m.to_mesh()
    return trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3], faces=np.asarray(mesh.tri_verts))


def parts():
    return {"base": make_base(), "clamp": make_clamp(), "carrier": make_carrier(), "diffuser": make_diffuser(),
            "dome": make_dome()}


def print_pose(name, m):
    """Положення на столі принтера: найбільша пласка грань донизу, без підтримок."""
    if name in ("dome", "diffuser"):
        m = m.rotate([180, 0, 0])          # ковпак і розсіювач друкуються догори дном
    if name == "clamp":
        m = m.rotate([0, 90, 0])           # рамка лежить плазом
    lo = m.bounding_box()[:3]
    return m.translate([-lo[0], -lo[1], -lo[2]])


# Без AMS кожен колір друкується окремою плитою
PLATES = {"plate_black": ["base"], "plate_white": ["carrier", "diffuser", "clamp"], "plate_clear": ["dome"]}


def export_plate(path, posed):
    """Кілька деталей на одному столі в ряд із проміжком 8 мм, окремими об'єктами у 3MF."""
    scene, x = trimesh.Scene(), 0.0
    for name, m in posed.items():
        scene.add_geometry(to_trimesh(m.translate([x, 0, 0])), node_name=name, geom_name=name)
        x += m.bounding_box()[3] + 8
    scene.export(path)


if __name__ == "__main__":
    out = pathlib.Path(__file__).parent / "stl"
    out.mkdir(exist_ok=True)
    built = parts()
    posed = {name: print_pose(name, m) for name, m in built.items()}
    for name, m in posed.items():
        to_trimesh(m).export(out / f"{name}.stl")
        bb = m.bounding_box()
        print(f"{name:8s} {bb[3]:.1f} x {bb[4]:.1f} x {bb[5]:.1f} мм, {built[name].volume() / 1000:.1f} см³")
    for plate, names in PLATES.items():
        export_plate(out / f"{plate}.3mf", {n: posed[n] for n in names})
    print(f"Зібраний корпус: Ø{2 * R_OUT:.0f} x {HEIGHT:.1f} мм")
