assert(fs.mkdirs("tests/.tmp"))
assert(type(audio) == "table")
assert(require("audio") == audio)

local tone = audio.tone(440, 0.02, {
    sample_rate = 22050, channels = 2, amplitude = 0.5,
    waveform = "sine", attack = 0.001, release = 0.002, pan = -0.25,
})
assert(type(tone) == "userdata")
assert(tone:channels() == 2)
assert(tone:sample_rate() == 22050)
assert(tone:frames() == 441)
assert(math.abs(tone:duration() - 0.02) < 0.000001)
assert(tone:info().frames == 441)
tone:set(10, 1, 0.5)
assert(tone:get(10, 1) == 0.5)
tone:fade(0.001, 0.001):gain(0.8):lowpass(4000):highpass(40)
tone:delay(0.001, 0.2, 0.1):normalize(0.8)

for _, waveform in ipairs({"square", "saw", "triangle", "noise"}) do
    local generated = audio.tone(220, 0.005, {waveform=waveform})
    assert(generated:frames() == 221)
end

local silence = audio.silence(0.03, 22050, 2)
local mixed = audio.mix(tone, silence)
assert(mixed:frames() == silence:frames())
local resampled = mixed:resample(11025)
assert(resampled:sample_rate() == 11025)
assert(resampled:channels() == 2)

assert(mixed:save("tests/.tmp/audio-pcm.wav"))
local pcm, pcmError = audio.load("tests/.tmp/audio-pcm.wav")
assert(pcm, pcmError)
assert(pcm:channels() == 2 and pcm:sample_rate() == 22050)
assert(pcm:frames() == mixed:frames())
assert(pcm:save("tests/.tmp/audio-float.wav", "float"))
local floating, floatError = audio.load("tests/.tmp/audio-float.wav")
assert(floating, floatError)
assert(floating:frames() == pcm:frames())
assert(math.abs(floating:get(20, 1) - pcm:get(20, 1)) < 0.0001)

local invalid, invalidError = audio.load("tests/libraries.lua")
assert(invalid == nil and invalidError:find("RIFF"))
if not audio.playback_available() then
    local unavailable, message = tone:play()
    assert(unavailable == nil and message:find("disabled"))
else
    -- A machine without an audio device may reject playback; compilation and
    -- DSP remain supported. If a device exists, exercise its full lifecycle.
    local playing = tone:play()
    if playing then tone:wait() end
    tone:stop()
end
print("LuaC native audio: OK")
