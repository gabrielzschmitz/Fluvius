import numpy as np
import matplotlib.pyplot as plt

# Fixed smoothing length parameter as per Müller et al. (2003)
h = 1.0

# 'r' is the independent variable representing distance along the axis through the center
r_axis = np.linspace(-h, h, 1000)

def get_poly6(r, h):
    dist = np.abs(r)
    w = np.zeros_like(dist)
    grad = np.zeros_like(dist)
    lap = np.zeros_like(dist)

    mask = dist <= h
    rm = dist[mask]

    c_w = 315.0 / (64.0 * np.pi * h**9)
    w[mask] = c_w * (h**2 - rm**2)**3
    grad[mask] = c_w * 6.0 * rm * (h**2 - rm**2)**2
    # Scaled by 0.1 to match Müller's plot scaling
    lap[mask] = 0.1 * c_w * 6.0 * (h**2 - rm**2) * (7.0 * rm**2 - 3.0 * h**2)
    return w, grad, lap

def get_spiky(r, h):
    dist = np.abs(r)
    w = np.zeros_like(dist)
    grad = np.zeros_like(dist)
    lap = np.zeros_like(dist)

    mask = (dist <= h)
    rm = np.where(dist[mask] < 1e-5, 1e-5, dist[mask])

    c_w = 15.0 / (np.pi * h**6)
    w[mask] = c_w * (h - rm)**3
    grad[mask] = c_w * 3.0 * (h - rm)**2
    # Scaled by 0.1 to match Müller's plot scaling
    lap[mask] = 0.1 * c_w * 6.0 * (2.0 * rm - h) * (h - rm) / rm
    return w, grad, lap

def get_viscosity(r, h):
    dist = np.abs(r)
    w = np.zeros_like(dist)
    grad = np.zeros_like(dist)
    lap = np.zeros_like(dist)

    mask = (dist <= h)
    rm = np.where(dist[mask] < 1e-5, 1e-5, dist[mask])

    c_w = 15.0 / (2.0 * np.pi * h**3)
    w[mask] = c_w * (-rm**3 / (2.0 * h**3) + rm**2 / h**2 + h / (2.0 * rm) - 1.0)
    grad[mask] = c_w * (3.0 * rm**2 / (2.0 * h**3) - 2.0 * rm / h**2 + h / (2.0 * rm**2))
    # Scaled by 0.1 to match Müller's plot scaling
    lap[mask] = 0.1 * c_w * 6.0 * (h - rm) / h**3
    return w, grad, lap

# Set up subplots with NO horizontal spacing (wspace=0)
fig, axes = plt.subplots(
    1, 3,
    figsize=(16, 5.5),
    sharex=True
)

kernel_specs = [
    {
        "name": r"Poly6 $W_{\rho}(r,h=1)$", 
        "func": get_poly6, 
        "color": "#1f77b4",
        "ylim": (-3.5, 3.5), 
        "yticks": [-3, -2, -1, 1, 2, 3], # Omit 0 to avoid cluttering origin
        "show_r_label": False
    },
    {
        "name": r"Spike $W_{s}(r,h=1)$", 
        "func": get_spiky, 
        "color": "#d62728", 
        "ylim": (-3.5, 15.5), 
        "yticks": [2, 4, 6, 8, 10, 12, 14],
        "show_r_label": False
    },
    {
        "name": r"Viscosity $W_{\nu}(r,h=1)$", 
        "func": get_viscosity, 
        "color": "#2ca02c",
        "ylim": (-0.5, 2.1), # Extended to -0.5 so its baseline aligns perfectly with the others
        "yticks": [0.2, 0.4, 0.6, 0.8, 1.0, 1.2, 1.4, 1.6, 1.8, 2.0],
        "show_r_label": True # Only label the final axis
    }
]

for idx, (ax, k) in enumerate(zip(axes, kernel_specs)):
    w, grad, lap = k["func"](r_axis, h)

    # Solid Thick: Kernel profile
    ax.plot(r_axis, w, color=k["color"], lw=3.5)

    # Solid Thin: Gradient magnitude profile
    ax.plot(r_axis, grad, color=k["color"], lw=1.5, alpha=0.8)

    # Dashed Thin: 10% Scaled Laplacian profile
    ax.plot(r_axis, lap, color=k["color"], lw=1.5, ls=(0, (5, 3)))

    # Subplot Titles
    ax.set_title(k["name"], fontsize=15, fontweight='bold', pad=15)
    
    # Precision limits to prevent subplots overlapping visually
    ax.set_xlim(-1.0, 1.0)
    ax.set_ylim(k["ylim"])
    ax.set_yticks(k["yticks"])
    
    # Custom X-ticks to mimic reference document spacing
    ax.set_xticks([-1.0, -0.8, -0.6, -0.4, -0.2, 0.2, 0.4, 0.6, 0.8, 1.0])

    # Centered spine placement strategy
    ax.spines['left'].set_position('zero')
    ax.spines['bottom'].set_position('zero')
    ax.spines['right'].set_color('none')
    ax.spines['top'].set_color('none')

    # Add the single trailing axis name if specified
    ax.set_xlabel("r", fontsize=12, fontweight='bold', loc='right')

    ax.xaxis.set_tick_params(labelsize=10)
    ax.yaxis.set_tick_params(labelsize=10)
    ax.grid(True, linestyle=":", alpha=0.4)

# Adjust remaining outer bounding box constraints cleanly
fig.align_ylabels(axes)
plt.subplots_adjust(left=0.05, right=0.95, top=0.85, bottom=0.15)
plt.subplots_adjust(wspace=0.1)
plt.show()
