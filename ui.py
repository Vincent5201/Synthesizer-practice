import tkinter as tk
from tkinter import ttk
import subprocess


class VoiceFrame(ttk.LabelFrame):
    def __init__(self, master, idx, on_delete):
        super().__init__(master, text=f"Voice {idx}")
        self.idx = idx
        self.on_delete = on_delete

        self.wave = tk.StringVar(value="sawtooth")
        self.attack = tk.DoubleVar(value=0.04)
        self.decay = tk.DoubleVar(value=0.05)
        self.sustain = tk.DoubleVar(value=0.9)
        self.release = tk.DoubleVar(value=0.1)
        self.cutoff = tk.IntVar(value=900)
        self.resonance = tk.DoubleVar(value=0.05)
        self.scale = tk.IntVar(value=0)
        self.lfo = tk.DoubleVar(value=4.8)
        self.vib = tk.DoubleVar(value=0.15)
        self.mel = tk.StringVar(
            value="60 60 67 67 69 69 67 0 65 65 64 64 62 62 60 0"
        )
        self.bts = tk.StringVar(
            value="4 4 4 4 4 4 2 2 4 4 4 4 4 4 2 2"
        )
        # 60 64 66 67 0 69 67 65 64 0
        # 2 2 2 2 2 2 2 2 2 2
        self._build()

    def _slider(self, row, text, var, frm, to, step):
        ttk.Label(self, text=text).grid(row=row, column=0, sticky="w")

        scale = tk.Scale(
            self,
            variable=var,
            from_=frm,
            to=to,
            resolution=step,
            orient="horizontal",
            showvalue=False
        )
        scale.grid(row=row, column=1, sticky="ew", padx=5)

        ttk.Entry(self, textvariable=var, width=6).grid(row=row, column=2)

    def _build(self):
        self.columnconfigure(1, weight=1)

        row = 0
        ttk.Label(self, text="Waveform").grid(row=row, column=0, sticky="w")
        frame = ttk.Frame(self)
        frame.grid(row=row, column=1, columnspan=2, sticky="w")

        for w in ["sine", "square", "sawtooth"]:
            ttk.Radiobutton(frame, text=w, value=w, variable=self.wave).pack(side="left")
        row += 1
        self._slider(row, "Attack", self.attack, 0, 1, 0.01)
        row += 1
        self._slider(row, "Decay", self.decay, 0, 1, 0.01)
        row += 1
        self._slider(row, "Sustain", self.sustain, 0, 1, 0.01)
        row += 1
        self._slider(row, "Release", self.release, 0, 1, 0.01)
        row += 1
        self._slider(row, "Cutoff", self.cutoff, 100, 5000, 10)
        row += 1
        self._slider(row, "Resonance", self.resonance, 0.01, 1, 0.01)
        row += 1
        self._slider(row, "LFO Hz", self.lfo, 0.1, 20, 0.1)
        row += 1
        self._slider(row, "Vib Hz", self.vib, 0.01, 10, 0.01)
        row += 1
        self._slider(row, "Scale", self.scale, -48, 48, 1)
        row += 1

        ttk.Label(self, text="Melody").grid(row=row, column=0, sticky="w")
        ttk.Entry(self, textvariable=self.mel, width=45)\
            .grid(row=row, column=1, columnspan=2, sticky="ew")
        row += 1

        ttk.Label(self, text="Beats").grid(row=row, column=0, sticky="w")
        ttk.Entry(self, textvariable=self.bts, width=45)\
            .grid(row=row, column=1, columnspan=2, sticky="ew")
        row += 1

        ttk.Button(self, text="🗑 Delete Voice",
                   command=self.on_delete)\
            .grid(row=row, column=0, columnspan=3, pady=5)

    def to_txt(self):
        i = self.idx
        return "\n".join([
            f"voice{i}_wave {self.wave.get()}",
            f"voice{i}_attack {self.attack.get()}",
            f"voice{i}_decay {self.decay.get()}",
            f"voice{i}_sustain {self.sustain.get()}",
            f"voice{i}_release {self.release.get()}",
            f"voice{i}_cutoff {self.cutoff.get()}",
            f"voice{i}_resonance {self.resonance.get()}",
            f"voice{i}_lfo_hz {self.lfo.get()}",
            f"voice{i}_vib_hz {self.vib.get()}",
            f"voice{i}_scale {self.scale.get()}",
            f"voice{i}_mel {self.mel.get()}",
            f"voice{i}_bts {self.bts.get()}",
            ""
        ])


class SynthUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("C Synth UI (TXT config)")
        self.geometry("900x600")

        self.voices = []

        canvas = tk.Canvas(self)
        scrollbar = ttk.Scrollbar(self, orient="vertical", command=canvas.yview)
        self.voice_container = ttk.Frame(canvas)

        self.voice_container.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )

        canvas.create_window((0, 0), window=self.voice_container, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)

        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        control = ttk.Frame(self)
        control.pack(fill="x", pady=5)

        ttk.Button(control, text="+ Add Voice",
                   command=self.add_voice).pack(side="left", padx=5)
        ttk.Button(control, text="Render WAV",
                   command=self.render).pack(side="right", padx=5)

        self.add_voice()

    def add_voice(self):
        vf = VoiceFrame(
            self.voice_container,
            len(self.voices),
            on_delete=lambda v=len(self.voices): self.delete_voice(v)
        )
        vf.pack(fill="x", padx=5, pady=5)
        self.voices.append(vf)

    def delete_voice(self, idx):
        self.voices[idx].destroy()
        del self.voices[idx]

        for i, v in enumerate(self.voices):
            v.idx = i
            v.configure(text=f"Voice {i}")

    def render(self):
        with open("config.txt", "w") as f:
            for v in self.voices:
                f.write(v.to_txt())

        subprocess.run(["./synth"])
        print("Rendered out.wav")


if __name__ == "__main__":
    SynthUI().mainloop()
