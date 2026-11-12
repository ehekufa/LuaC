-- PCM synthesis, effects, mixing and WAV output work without an audio device.
local lead = audio.tone(440, 0.35, {
    sample_rate = 44100, channels = 2, waveform = "triangle",
    amplitude = 0.35, attack = 0.01, release = 0.08, pan = -0.2,
})
local bass = audio.tone(110, 0.35, {
    sample_rate = 44100, channels = 2, waveform = "sine",
    amplitude = 0.25, attack = 0.01, release = 0.08, pan = 0.2,
})
local song = audio.mix(lead, bass)
song:lowpass(5000):fade(0.01, 0.05):normalize(0.8)
assert(song:save("luac-audio.wav"))
print("WAV:", song:frames(), "frames,", song:duration(), "seconds")

if audio.playback_available() then
    local ok, message = song:play()
    if ok then
        if not song:wait() then print("Playback device timed out") end
    else print("No playback device:", message) end
else
    print("Playback backend is disabled; WAV was still created")
end
