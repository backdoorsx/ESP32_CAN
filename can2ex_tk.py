
# -----------------------------
#  CAN LOG VIEWER – TKINTER GUI
# -----------------------------
#  Left side = list of CAN IDs
#  Click ID = opens matplotlib window with plot_obd8_hex_time()
#  Fully compatible with your code and matplotlib theme
# -----------------------------

import tkinter as tk
from tkinter import ttk
import matplotlib.pyplot as plt
import numpy as np

# ---------------------------------------------------------
#  YOUR MATPLOTLIB DARK THEME SETTINGS (unchanged)
# ---------------------------------------------------------

plt.style.use('dark_background')

plt.rcParams['grid.color'] = '#666666'
plt.rcParams['grid.linestyle'] = '-'
plt.rcParams['axes.facecolor'] = '#333333'
plt.rcParams['figure.facecolor'] = '#333333'

plt.rcParams['axes.prop_cycle'] = plt.cycler(color=[
    '#ff5555',  # 1 - červená
    '#55ff55',  # 2 - zelená
    '#5599ff',  # 3 - modrá
    '#ffff55',  # 4 - žltá
    '#ff55ff',  # 5 - magenta
    '#55ffff',  # 6 - cyan
    '#ffaa55',  # 7 - oranžová
    '#aaaaaa',  # 8 - sivá
])

plt.rcParams['lines.linewidth'] = 4.5


# ---------------------------------------------------------
#  TKINTER SUPPORT FUNCTIONS
# ---------------------------------------------------------

def extract_sorted_ids(data):
    """ Vrati zoznam CAN ID zoradený podľa počtu frame-ov """
    id_counts = {}
    for row in data:
        cid = row[1]
        id_counts[cid] = id_counts.get(cid, 0) + 1

    return sorted(id_counts.items(), key=lambda x: x[1], reverse=True)


def filter_frames_by_id(data, selected_id):
    """ Vrati len tie riadky, ktoré patria k vybranému CAN ID """
    output = []
    for row in data:
        if row[1] == selected_id:
            # extrakcia presne ako vo tvojom kóde
            output.append([
                row[0], row[4], row[5], row[6],
                row[7], row[8], row[9], row[10], row[11]
            ])
    return output


# ---------------------------------------------------------
#  YOUR ORIGINAL MATPLOTLIB FUNCTION (MINOR FIX: show())
# ---------------------------------------------------------

def plot_hex(data):

    arr = np.array(data)

    if arr.shape[1] != 9:
        raise ValueError("Očakávam presne 9 stĺpcov: [cas_ms, d0–d7].")

    time_ms = arr[:, 0].astype(float)
    arr_dec = np.vectorize(lambda x: int(x, 16))(arr[:, 1:])
    use_markers = len(time_ms) <= 1000

    fig = plt.figure(figsize=(16, 9))
    fig.canvas.manager.set_window_title("CAN Plot")
    ax = fig.add_axes([0.05, 0.05, 0.9, 0.9])

    lines = []
    for i in range(8):
        line, = ax.plot(
            time_ms,
            arr_dec[:, i],
            label=f"d{i}",
            marker='o' if use_markers else None,
            markersize=5 if use_markers else 0,
            linewidth=1.0
        )
        lines.append(line)

    leg = ax.legend(
        loc="upper left",
        fontsize=12,
        markerscale=1,
        handlelength=1,
        framealpha=0.9,
        borderpad=0.2
    )

    # text v legende klikateľný
    for line, legline in zip(lines, leg.get_texts()):
        legline.set_picker(True)
        legline._linked_line = line

    def on_pick(event):
        legline = event.artist
        orig = legline._linked_line
        vis = not orig.get_visible()
        orig.set_visible(vis)
        legline.set_alpha(1.0 if vis else 0.3)
        fig.canvas.draw_idle()

    fig.canvas.mpl_connect("pick_event", on_pick)

    ax.set_xlabel("čas [ms]")
    ax.set_ylabel("hodnota (dec)")
    ax.grid(True)

    plt.show()

def plot_from_bytes(data, scale=1.0, abit=1, bbit=2, tickx=1000):
    '''
    rpm real = 0x201 (d0 << 8) | d1
    speed real = 0x201 (d4 << 8) | d5 / 100
    '''
    rpm_raw = []
    times_ms = []
    print(f'plot_from_bytes(data, {scale}, {abit}, {bbit}, {tickx})')
    for frame in data:
        cas_v_ms = int(frame[0])
        d2 = int(frame[abit+1], 16)
        d3 = int(frame[bbit+1], 16)

        val = (d2 << 8) | d3
        rpm_raw.append(val)
        times_ms.append(cas_v_ms)

    rpm_raw = np.array(rpm_raw)
    rpm_scaled = rpm_raw * scale
    
    x = np.array(times_ms)
    
    plt.figure(figsize=(14, 6))
    plt.plot(x, rpm_scaled, linewidth=1.8, color="blue", label=f"RPM (scale = {scale})")
    plt.plot(x, rpm_raw, linewidth=1.2, color="gray", alpha=0.5, label="Raw DEC")

    plt.title("unknown – abit+bbit → DEC")
    plt.xlabel("Index vzorky")
    plt.ylabel("-")
    
    # Vlastné xticks – hustejšia mriežka každých 100 ms
    x_start = x[0]
    x_end = x[-1]
    plt.xticks(np.arange(x_start, x_end + 1, tickx), rotation=90)

##    # Vlastné yticks – každých 500 RPM
##    y_min = int(np.min(rpm_scaled) // 100 * 100)
##    y_max = int(np.max(rpm_scaled) // 100 * 100 + 100)
##    plt.yticks(np.arange(y_min, y_max + 1, 100))

    plt.grid(True, linestyle='--', alpha=0.5)
    plt.legend()
    plt.tight_layout()
    plt.show()
    

# ---------------------------------------------------------
#  TKINTER GUI – LEFT PANEL WITH ID LIST
# ---------------------------------------------------------


def start_gui(data):

    sorted_ids = extract_sorted_ids(data)

    window = tk.Tk()
    window.title("CAN Log Viewer")
    window.geometry("480x800")
    window.configure(bg="#222222")

    # --- CHECKBOX PANEL ---
    tk.Label(window, text="Vyber bajty (bx << 8) | by",
             fg="white", bg="#222222", font=("Consolas", 12, "bold")).pack(pady=5)

    check_frame = tk.Frame(window, bg="#222222")
    check_frame.pack()

    byte_vars = []
    for i in range(8):
        var = tk.IntVar()
        cb = tk.Checkbutton(
            check_frame, text=f"b{i}", variable=var,
            fg="white", bg="#222222", selectcolor="#333333",
            font=("Consolas", 11)
        )
        cb.grid(row=0, column=i, padx=3)
        byte_vars.append(var)

    
    # --- SCALE (v jednom riadku) ---
    scale_frame = tk.Frame(window, bg="#222222")
    scale_frame.pack(pady=3)

    tk.Label(scale_frame, text="Scale:", fg="white", bg="#222222").pack(side=tk.LEFT)
    scale_entry = tk.Entry(scale_frame, width=8)
    scale_entry.insert(0, "1.0")
    scale_entry.pack(side=tk.LEFT, padx=5)


    
    # --- TICKX (v jednom riadku) ---
    tick_frame = tk.Frame(window, bg="#222222")
    tick_frame.pack(pady=3)

    tk.Label(tick_frame, text="TickX:", fg="white", bg="#222222").pack(side=tk.LEFT)
    tick_entry = tk.Entry(tick_frame, width=8)
    tick_entry.insert(0, "1000")
    tick_entry.pack(side=tk.LEFT, padx=5)


    # --- LIST OF IDs ---
    tk.Label(window, text="CAN ID:", fg="white", bg="#222222",
             font=("Consolas", 14, "bold")).pack(pady=10)

    frame_canvas = tk.Frame(window)
    frame_canvas.pack(fill=tk.BOTH, expand=True)

    canvas = tk.Canvas(frame_canvas, bg="#222222", highlightthickness=0)
    scrollbar = ttk.Scrollbar(frame_canvas, orient="vertical", command=canvas.yview)
    scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

    canvas.configure(yscrollcommand=scrollbar.set)
    canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

    inner = tk.Frame(canvas, bg="#222222")
    canvas.create_window((0, 0), window=inner, anchor="nw")

    selected_id = tk.StringVar()

    # --- CLICK HANDLER ---
    def on_click():
        cid = selected_id.get()

        # which D-bytes?
        selected_bits = [i for i, v in enumerate(byte_vars) if v.get() == 1]

        # scale
        try: scale = float(scale_entry.get())
        except: scale = 1.0

        # tickx
        try: tickx = int(tick_entry.get())
        except: tickx = 1000

        filtered = filter_frames_by_id(data, cid)

        if len(selected_bits) == 0:
            # no checkbox → original function
            plot_hex(filtered)
        elif len(selected_bits) == 2:
            # 2 checkboxes → new function
            abit, bbit = selected_bits
            plot_from_bytes(filtered, scale=scale, abit=abit, bbit=bbit, tickx=tickx)
        else:
            print("Vyber 0 alebo 2 D-bajty (b0..b7).")

    # --- RADIOBUTTONS FOR IDs ---
    for cid, count in sorted_ids:
        rb = tk.Radiobutton(
            inner, text=f"{cid}   ({count})", value=cid,
            variable=selected_id, command=on_click,
            fg="white", bg="#222222", selectcolor="#333333",
            font=("Consolas", 11), anchor="w"
        )
        rb.pack(fill=tk.X, padx=5, pady=2)

    inner.update_idletasks()
    canvas.config(scrollregion=canvas.bbox("all"))

    window.mainloop()


# ---------------------------------------------------------
#  MAIN – LOAD CSV AND START GUI
# ---------------------------------------------------------

if __name__ == "__main__":

    data_log = "canex.csv"
    data = []

    with open(data_log, "r") as f:
        for line in f:
            cols = line.strip().split(",")
            data.append(cols)

    start_gui(data)
