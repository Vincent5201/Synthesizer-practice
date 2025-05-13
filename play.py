import pyaudio

SAMPLE_RATE = 11025

NOTES = ["C3", "C3#", "D3", "D3#", "E3", "F3", "F3#", "G3", "G3#", "A3", "A3#", "B3",
         "C4", "C4#", "D4", "D4#", "E4", "F4", "F4#", "G4", "G4#", "A4", "A4#", "B4",
         "C5", "C5#", "D5", "D5#", "E5", "F5", "F5#", "G5", "G5#", "A5", "A5#", "B5" ]

for note in NOTES:
    with open(f"basic sounds//{note}.raw", "rb") as f:
        raw_data = f.read()

    p = pyaudio.PyAudio()
    stream = p.open(format=pyaudio.paInt16,
                    channels=1,
                    rate=SAMPLE_RATE,
                    output=True)

    stream.write(raw_data)

    stream.stop_stream()
    stream.close()
    p.terminate()