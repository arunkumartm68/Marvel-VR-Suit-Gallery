"""Search claw poses that reach round to the FRONT of the suit without touching the body (or each other).

  python claw_pose_search.py <claw_rig.npz> [min_clearance_m] [fixed upper pose as a JSON list]

claw_rig.npz comes from claw_rig_data.py (Blender). Needs numpy + scipy (system Python, not Blender's). The pose in
prepare_ironspider_for_quest.py came from two runs: the full search with the default 4.5 cm clearance (upper claws),
then the lower claws alone with 8 cm (python claw_pose_search.py claw_rig.npz 0.08 "[<upper values>]").

Rig per claw (+x side; the -x side mirrors it): ball 1 (root, on the backplate) -> ball 2 -> hinge 1 -> hinge 2 -> hinge 3 -> blade.
Both balls are ball joints, the hinges bend in one plane. Parameters:
  root_yaw, root_elev     direction of the short ball1->ball2 link (yaw 0 = straight back, 90 = own side, 180 = ahead)
  yaw, roll               orientation of the hinge plane from ball 2 on (roll tilts the plane about its first link)
  e2..e5                  in-plane angles of links ball2->h1, h1->h2, h2->h3, blade (lower blade: solved to touch the floor)
"""
import json
import math
import sys

import numpy as np
from scipy.optimize import differential_evolution
from scipy.spatial import cKDTree

rig = np.load(sys.argv[1])
FLOOR = float(rig["floor_z"])
PIVOT = rig["pivot"]
BODY = cKDTree(rig["body"])


def rx(a):
    c, s = math.cos(a), math.sin(a)
    return np.array(((1, 0, 0), (0, c, -s), (0, s, c)))


def ry(a):
    c, s = math.cos(a), math.sin(a)
    return np.array(((c, 0, s), (0, 1, 0), (-s, 0, c)))


def rz(a):
    c, s = math.cos(a), math.sin(a)
    return np.array(((c, -s, 0), (s, c, 0), (0, 0, 1)))


class Claw:
    def __init__(self, kind):
        self.J = rig[kind + "_joints"]  # ball1, ball2, h1, h2, h3, tip
        self.links = [rig["%s_link%d" % (kind, i)] for i in range(6)]
        d = np.diff(self.J, axis=0)
        self.theta = np.arctan2(d[:, 2], d[:, 1])  # original in-plane angle of links 1..5

    def transforms(self, p, e5=None):
        root_yaw, root_elev, yaw, roll, e2, e3, e4 = p[:7]
        e5 = p[7] if e5 is None else e5
        J, th = self.J, self.theta
        Rr = rz(-math.radians(root_yaw)) @ rx(math.radians(root_elev) - th[0])
        out = [(Rr, J[0], J[0])]
        nj = J[0] + Rr @ (J[1] - J[0])
        base = rz(-math.radians(yaw)) @ ry(math.radians(roll))
        for i, e in enumerate((e2, e3, e4, e5), start=1):
            R = base @ rx(math.radians(e) - th[i])
            out.append((R, J[i], nj))
            nj = nj + R @ (J[i + 1] - J[i])
        return out, nj  # nj = new tip

    def pose(self, p, e5=None):
        tf, tip = self.transforms(p, e5)
        pts = [((self.links[i + 1] - o) @ R.T + n) for i, (R, o, n) in enumerate(tf)]
        joints = [tf[0][2]] + [n for (_, _, n) in tf[1:]] + [tip]
        return pts, np.array(joints)

    def solve_blade(self, p):
        """Blade angle at which the blade's lowest point rests on the floor (lower claws)."""
        lo, hi = -100.0, 30.0
        for _ in range(28):
            mid = (lo + hi) / 2
            tf, _ = self.transforms(p, mid)
            R, o, n = tf[4]
            lowest = ((self.links[5] - o) @ R.T + n)[:, 2].min()
            if lowest < FLOOR + 0.004:
                lo = mid
            else:
                hi = mid
        return (lo + hi) / 2


def blade_dir(joints):
    d = joints[-1] - joints[-2]
    return d / np.linalg.norm(d)


def link_clearances(pts):
    return [float(BODY.query(p)[0].min()) for p in pts]


FWD = lambda y: PIVOT[1] - y  # metres in front of the pivot (the suit faces -y)


# Front-view (x, z) silhouette of the approved "back" pose: hinge 1, hinge 2, hinge 3, tip. The claws keep this X seen
# from the front, but now reach forward instead of back.
SIL_U = np.array(((0.469, 0.295), (0.840, 0.540), (1.153, 0.462), (1.424, 0.006)))
SIL_L = np.array(((0.646, -0.4385), (0.959, -0.81), (1.041, -1.24), (1.327, FLOOR)))


CLEAR = float(sys.argv[2]) if len(sys.argv) > 2 else 0.045


def shared_costs(e, cl):
    """Body clearance (the short root link may come closer) and hinge bends: a smooth arch, no bend over 75 degrees."""
    c = 400000 * max(0.0, 0.02 - cl[0]) ** 2 + 400000 * sum(max(0.0, CLEAR - x) ** 2 for x in cl[1:])
    c += 0.002 * sum(max(0.0, e[i + 1] - e[i]) ** 2 for i in range(3))  # smooth arch: every hinge bends the same way (down)
    c += 0.0005 * sum(max(0.0, abs(e[i + 1] - e[i]) - 75) ** 2 for i in range(3))
    return c


def cost_upper(p, claw, t, verbose=False):
    pts, joints = claw.pose(p)
    cl = link_clearances(pts)
    c = shared_costs(list(p[4:8]), cl)
    c += t["w_sil"] * np.sum((joints[2:, [0, 2]] - SIL_U) ** 2)
    tip = joints[-1]
    c += 10 * (FWD(tip[1]) - t["fwd"]) ** 2
    c += 2 * np.sum((blade_dir(joints) - t["blade"]) ** 2)
    top = max(q[:, 2].max() for q in pts)
    c += 200 * max(0.0, top - t["top"]) ** 2
    # past ball 2 every joint is further forward than the previous one (the claw reaches round to the front)
    c += 50 * sum(max(0.0, joints[i + 1][1] - joints[i][1] + 0.02) ** 2 for i in range(2, 5))
    if verbose:
        return dict(cost=round(c, 4), clear_cm=[round(x * 100, 1) for x in cl], tip=tip.round(3).tolist(), fwd=round(FWD(tip[1]), 3),
                    blade=blade_dir(joints).round(2).tolist(), top=round(top, 3), joints=joints.round(3).tolist())
    return c


def cost_lower(p, claw, t, upper_tree, verbose=False):
    e5 = claw.solve_blade(p)
    q = np.concatenate([p[:7], [e5]])
    pts, joints = claw.pose(q)
    cl = link_clearances(pts)
    c = shared_costs(list(p[4:7]) + [e5], cl)
    lowest = pts[4][:, 2].min()
    c += 2000 * (lowest - FLOOR - 0.004) ** 2  # blade must reach the floor
    cc = min(float(upper_tree.query(x)[0].min()) for x in pts)
    c += 400000 * max(0.0, 0.05 - cc) ** 2
    c += t["w_sil"] * np.sum((joints[2:5, [0, 2]] - SIL_L[:3]) ** 2)
    tip = joints[-1]
    c += 10 * (tip[0] - t["tip_x"]) ** 2 + 10 * (FWD(tip[1]) - t["fwd"]) ** 2
    blade_elev = math.degrees(math.asin(blade_dir(joints)[2]))
    c += 0.02 * max(0.0, blade_elev - t["blade_elev"]) ** 2
    c += 200 * max(0.0, joints[2][2] - (claw.J[0][2] + 0.02)) ** 2  # no climbing over the arm
    c += 50 * sum(max(0.0, joints[i + 1][1] - joints[i][1] + 0.02) ** 2 for i in range(2, 5))
    if verbose:
        return dict(cost=round(c, 4), clear_cm=[round(x * 100, 1) for x in cl], claw_gap_cm=round(cc * 100, 1), lowest=round(float(lowest), 3),
                    tip=tip.round(3).tolist(), fwd=round(FWD(tip[1]), 3), blade_elev=round(blade_elev, 1), e5=round(e5, 1),
                    joints=joints.round(3).tolist())
    return c


if __name__ == "__main__":
    t_u = {"w_sil": 3.0, "fwd": 0.75, "blade": np.array((0.3, -0.4, -0.87)) / np.linalg.norm((0.3, -0.4, -0.87)), "top": 0.55}
    t_l = {"w_sil": 3.0, "tip_x": 1.2, "fwd": 0.75, "blade_elev": -35.0}
    upper, lower = Claw("upper"), Claw("lower")
    lower.links[5] = lower.links[5][::3]
    bounds_u = [(10, 130), (-60, 70), (60, 178), (-80, 80), (-60, 100), (-90, 90), (-100, 80), (-110, 40)]
    if len(sys.argv) > 3:  # fixed upper pose (JSON list) -> only search the lower claws
        pu = np.array(json.loads(sys.argv[3]))
    else:
        pu = differential_evolution(cost_upper, bounds_u, args=(upper, t_u), popsize=24, maxiter=200, tol=1e-8, seed=3, polish=True, workers=-1, updating="deferred").x
    info_u = cost_upper(pu, upper, t_u, verbose=True)
    upper_tree = cKDTree(np.concatenate(upper.pose(pu)[0]))
    bounds_l = [(10, 130), (-80, 30), (60, 178), (-80, 80), (-80, 30), (-95, 20), (-100, 10)]
    res_l = differential_evolution(cost_lower, bounds_l, args=(lower, t_l, upper_tree), popsize=24, maxiter=200, tol=1e-8, seed=4, polish=True, workers=-1, updating="deferred")
    pl = res_l.x
    info_l = cost_lower(pl, lower, t_l, upper_tree, verbose=True)
    names = ["root_yaw", "root_elevation", "yaw", "roll", "e2", "e3", "e4", "e5"]
    out = {"upper": dict(zip(names, [round(float(v), 2) for v in pu])), "lower": dict(zip(names[:7], [round(float(v), 2) for v in pl]))}
    out["lower"]["e5"] = info_l["e5"]
    print("POSE=" + json.dumps(out))
    print("UPPER=" + json.dumps(info_u))
    print("LOWER=" + json.dumps(info_l))
