import pandas as pd
import matplotlib.pyplot as plt

render = pd.read_csv("renderresult.csv")
headless = pd.read_csv("headlessresult.csv")

files = {
    "fps": ("results_fps.png", "QPS", "QPS vs Número de Partículas"),
    "time_per_step_ms": ("results_time.png", "Tempo de Passo (ms)", "Tempo de Passo vs Número de Partículas"),
}

threshold = 1000

for column, (outfile, ylabel, title) in files.items():
    fig, ax = plt.subplots(figsize=(8, 5))
    df_render = render[render["particles"] >= threshold]
    df_headless = headless[headless["particles"] >= threshold]
    ax.plot(df_render["particles"], df_render[column], color="red", label="Renderização")
    ax.plot(df_headless["particles"], df_headless[column], color="yellow", label="Headless")
    ax.set_xlabel("Número de Partículas")
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.legend()
    ax.grid(True)
    fig.tight_layout()
    fig.savefig(outfile, dpi=200)
    print(f"Saved {outfile}")

plt.show()
