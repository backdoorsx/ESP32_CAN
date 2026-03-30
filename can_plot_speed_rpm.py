
#ts_MS,idcko,ex,length,d0,d1,d2,d3,d4,d5,d6,d7

import matplotlib.pyplot as plt
import numpy as np


def sort_can_counts(data_dict):
    return dict(sorted(data_dict.items(), key=lambda x: x[1], reverse=True))


def plot_hex_array_raw(data):
    # Prevod HEX -> DEC
    arr_dec = np.array([int(x, 16) for x in data])

    # X osi budú indexy 0..N-1
    x = np.arange(len(arr_dec))

    plt.figure(figsize=(14, 6))
    plt.plot(x, arr_dec, linewidth=1.6)
    plt.title("HEX → DEC graf")
    plt.xlabel("Index")
    plt.ylabel("Hodnota (dec)")
    plt.grid(True)
    plt.tight_layout()
    plt.show()

# speed real
def plot_speed_from_frames(data):

    speed_raw = []

    for frame in data:
        d4 = int(frame[5], 16)
        d5 = int(frame[6], 16)

        val = (d4 << 8) | d5
        speed_raw.append(val)

    speed_raw = np.array(speed_raw)
    speed_kmh = speed_raw / 100

    x = np.arange(len(speed_raw))

    plt.figure(figsize=(14, 6))
    plt.plot(x, speed_kmh, linewidth=1.8, color="red", label="Speed (km/h)")
    plt.plot(x, speed_raw, linewidth=1.2, color="gray", alpha=0.4, label="Raw")

    plt.title("Speed – (d4<<8 | d5) / 100")
    plt.xlabel("Index vzorky")
    plt.ylabel("km/h")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    plt.show()

#rpm real
def plot_rpm_from_bytes(data, scale=1.0):

    rpm_raw = []
    
    for frame in data:
        d0 = int(frame[1], 16)
        d1 = int(frame[2], 16)

        val = (d0 << 8) | d1
        rpm_raw.append(val)

    rpm_raw = np.array(rpm_raw)
    rpm_scaled = rpm_raw * scale

    x = np.arange(len(rpm_raw))

    plt.figure(figsize=(14, 6))
    plt.plot(x, rpm_scaled, linewidth=1.8, color="blue", label=f"RPM (scale = {scale})")
    plt.plot(x, rpm_raw, linewidth=1.2, color="gray", alpha=0.5, label="Raw DEC")

    plt.title("RPM – d0+d1 → DEC")
    plt.xlabel("Index vzorky")
    plt.ylabel("RPM")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    plt.show()

#neviem 2a3 6a7
def plot_23_from_bytes(data, scale=1.0):

    rpm_raw = []
    
    for frame in data:
        d2 = int(frame[3], 16)
        d3 = int(frame[4], 16)

        val = (d2 << 8) | d3
        rpm_raw.append(val)

    rpm_raw = np.array(rpm_raw)
    rpm_scaled = rpm_raw

    x = np.arange(len(rpm_raw))

    plt.figure(figsize=(14, 6))
    plt.plot(x, rpm_scaled, linewidth=1.8, color="blue", label=f"RPM (scale = {scale})")
    plt.plot(x, rpm_raw, linewidth=1.2, color="gray", alpha=0.5, label="Raw DEC")

    plt.title("unknown – d2+d3 → DEC")
    plt.xlabel("Index vzorky")
    plt.ylabel("RPM")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    plt.show()

def plot_obd8_hex_time(data):

    arr = np.array(data)

    if arr.shape[1] != 9:
        raise ValueError("Očakávam presne 9 stĺpcov: [cas_ms, d0–d7].")

    time_ms = arr[:, 0].astype(float)

    arr_dec = np.vectorize(lambda x: int(x, 16))(arr[:, 1:])

    use_markers = len(time_ms) <= 1000

    fig = plt.figure(figsize=(16, 9))
    ax = fig.add_axes([0.05, 0.05, 0.9, 0.9])  

    lines = []
    for i in range(8):
        if use_markers:
            line, = ax.plot(
                time_ms, arr_dec[:, i], label=f"d{i}",
                marker='o', markersize=5, linewidth=1.5
            )
        else:
            line, = ax.plot(
                time_ms, arr_dec[:, i], label=f"d{i}",
                linewidth=1.5
            )
        lines.append(line)

    leg = ax.legend(
        loc="upper left",
        fontsize=14,
        markerscale=3,
        handlelength=3,
        framealpha=0.7,
        borderpad=1.0
    )

    for line, legline in zip(lines, leg.get_lines()):
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

    ax.set_title(" ", fontsize=16)
    ax.set_xlabel("čas [ms]", fontsize=14)
    ax.set_ylabel("hodnota (dec)", fontsize=14)
    ax.grid(True)

    plt.show()

if 1 == 1:
    data_log = 'canex.csv'

    with open(data_log, 'r') as f:
        data = f.readlines()

    print(len(data))

    ids = {}
    for line in data:
        line = line.replace('\n','')
        idcko = line.split(',')[1]
        if idcko not in ids:
            ids[idcko] = 1
        else:
            ids[idcko] = ids[idcko]+1

    sorted_data = sort_can_counts(ids)

    for key, value in sorted_data.items():
        print(key, value)

    datax = []
    for line in data:
        line = line.replace('\n','')
        line = line.split(',')
        if '0x00000201' == line[1]:
            datax.append([line[0],line[4],line[5],line[6],line[7],line[8],line[9],line[10],line[11]])

##    with open('speed_0x201.log', 'w') as f:
##        f.write(str(datax))
##        
    #datax = datax[:20000]
##    print(datax[0])
##    print(datax[-1])
    #plot_23_from_bytes(datax)
    #plot_obd8_hex_time(datax)

    plot_rpm_from_bytes(datax)
    #plot_speed_from_frames(datax)

##    d5 = []
##    for data in datax:
##        d5.append(data[5])
##    with open('speed_0x201_d5.log', 'w') as f:
##        f.write(str(d5))
##    
    #plot_hex_array(d5)
    

    # SPEED = 0x00000216 d[2], d[0]
    # BREAK/SPEED = 0x00000215 BREAK d[2], SPEED d[0]
    # SPEED = 0x0000020F d[2]
    # SPEED = 0x0000020E d[0]
    # ECU SPEED/RPM = 0x00000201 d[0] a d[4]

