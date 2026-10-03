// Built-in capture block: the wrapper must sound exactly like NAM itself, the
// IR stage must convolve, and (when the NAM LV2 plugin is installed) the block
// must null against the plugin running the same model. Also prints CPU cost.
#include "CaptureNode.h"
#include "LV2Host.h"
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include <lilv/lilv.h>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

namespace {
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

std::string modelPath() {
    if (const char* env = std::getenv("RIGROOM_TEST_NAM")) return env;
    return std::string(NAM_EXAMPLE_MODELS) + "/wavenet_a1_standard.nam";
}

std::vector<float> testSignal(int frames) {
    // A decaying guitar-like burst plus a little noise, at a realistic level.
    std::vector<float> s(frames);
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0.0f, 0.01f);
    for (int i = 0; i < frames; ++i) {
        const double t = i / kRate;
        const double env = std::exp(-3.0 * std::fmod(t, 1.0));
        s[i] = static_cast<float>(0.3 * env * (std::sin(2 * M_PI * 110 * t) + 0.5 * std::sin(2 * M_PI * 220.5 * t))) + noise(rng);
    }
    return s;
}

// Runs `signal` through a node in engine-sized blocks (mono in -> mono out).
std::vector<float> run(AudioNode& node, const std::vector<float>& signal, double* seconds = nullptr) {
    std::vector<float> out(signal.size());
    float* in = nullptr;
    float* o = nullptr;
    for (auto& p : node.getPorts()) {
        if (p.isInput && !in) in = p.buffer;
        if (!p.isInput && !o) o = p.buffer;
    }
    assert(in && o);
    const auto start = std::chrono::steady_clock::now();
    for (size_t pos = 0; pos + kBlock <= signal.size(); pos += kBlock) {
        std::memcpy(in, signal.data() + pos, kBlock * sizeof(float));
        node.processBlock(kBlock);
        std::memcpy(out.data() + pos, o, kBlock * sizeof(float));
    }
    if (seconds) *seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return out;
}

double rms(const std::vector<float>& v, size_t from = 0) {
    double sum = 0;
    for (size_t i = from; i < v.size(); ++i) sum += double(v[i]) * v[i];
    return std::sqrt(sum / std::max<size_t>(1, v.size() - from));
}

double db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

void writeMonoFloatWav(const std::string& path, const std::vector<float>& samples, uint32_t rate) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * 4);
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(3); u16(1); u32(rate); u32(rate * 4); u16(4); u16(32);
    f.write("data", 4); u32(dataBytes);
    f.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
}

void testWrapperMatchesNamCore() {
    CaptureNode node;
    node.prepareBlock(kRate, kBlock);
    std::string error;
    const bool loaded = node.loadModel(modelPath(), &error);
    if (!loaded) std::cerr << "model load failed: " << error << "\n";
    assert(loaded);
    assert(node.modelSampleRate() == 48000.0);
    assert(!node.modelIsResampled());

    auto reference = nam::get_dsp(std::filesystem::path(modelPath()));
    reference->Reset(kRate, kBlock);
    const auto signal = testSignal(kBlock * 200);
    std::vector<float> expected(signal.size());
    for (size_t pos = 0; pos + kBlock <= signal.size(); pos += kBlock) {
        float* in[1] = {const_cast<float*>(signal.data() + pos)};
        float* out[1] = {expected.data() + pos};
        reference->process(in, out, kBlock);
    }
    // The block normalizes to NAM's -18 dB loudness target, like the plugin.
    if (reference->HasLoudness()) {
        const float g = std::pow(10.0f, static_cast<float>(-18.0 - reference->GetLoudness()) / 20.0f);
        for (float& v : expected) v *= g;
    }
    const auto got = run(node, signal);
    double maxDiff = 0;
    for (size_t i = 0; i < got.size(); ++i) maxDiff = std::max(maxDiff, double(std::abs(got[i] - expected[i])));
    std::cout << "wrapper vs NAM core: max diff " << maxDiff << ", output " << db(rms(got)) << " dBFS rms\n";
    assert(maxDiff < 1e-6);
    assert(rms(got) > 1e-4);
}

void testIrConvolvesAndCabToggles() {
    const auto dir = std::filesystem::temp_directory_path() / "rigroom-capture-test";
    std::filesystem::create_directories(dir);
    const std::string irPath = (dir / "delay.wav").string();
    // A pure 10-sample delay.
    std::vector<float> ir(64, 0.0f);
    ir[10] = 1.0f;
    writeMonoFloatWav(irPath, ir, 48000);

    CaptureNode node;
    node.prepareBlock(kRate, kBlock);
    std::string error;
    assert(node.loadIr(irPath, &error));
    const auto signal = testSignal(kBlock * 20);
    const auto out = run(node, signal);
    // NAM's IR stage trims every IR by 18 dB (as the NAM plugin does).
    const float gain = std::pow(10.0f, -18.0f / 20.0f);
    for (size_t i = 10; i < signal.size() - kBlock; ++i) assert(std::abs(out[i] - gain * signal[i - 10]) < 1e-5f);

    node.setParameter(CaptureNode::CabEnabled, 0.0f);
    const auto dry = run(node, signal);
    for (size_t i = 0; i < 1000; ++i) assert(std::abs(dry[i] - signal[i]) < 1e-6f);

    // Bad files are refused and leave the IR as it was.
    assert(!node.loadIr((dir / "missing.wav").string(), &error));
    assert(node.irPath() == irPath);
    std::filesystem::remove_all(dir);
    std::cout << "IR stage: ok\n";
}

// Room / reverb IRs are long: a 1.5 s echo in a 2 s IR must survive (NAM's own
// IR stage stops at 8192 samples).
void testLongIrKeepsItsTail() {
    const auto dir = std::filesystem::temp_directory_path() / "rigroom-capture-long-ir";
    std::filesystem::create_directories(dir);
    const std::string irPath = (dir / "room.wav").string();
    std::vector<float> ir(96000, 0.0f);
    ir[0] = 1.0f;
    ir[72000] = 0.5f;
    writeMonoFloatWav(irPath, ir, 48000);

    CaptureNode node;
    node.prepareBlock(kRate, kBlock);
    assert(node.loadIr(irPath));
    std::vector<float> click(kBlock * 300, 0.0f);
    click[0] = 1.0f;
    double seconds = 0;
    const auto out = run(node, click, &seconds);
    const float gain = std::pow(10.0f, -18.0f / 20.0f);
    assert(std::abs(out[0] - gain) < 1e-4f);
    assert(std::abs(out[72000] - 0.5f * gain) < 1e-4f);
    for (size_t i = 1; i < out.size(); ++i) {
        if (i != 72000) assert(std::abs(out[i]) < 1e-4f);
    }
    std::cout << "2 s IR: echo at 1.5 s kept; " << 100.0 * seconds / (click.size() / kRate) << " % of realtime\n";
    std::filesystem::remove_all(dir);
}

void testResamplesForeignRate() {
    CaptureNode node;
    node.prepareBlock(44100.0, kBlock);
    assert(node.loadModel(modelPath()));
    assert(node.modelIsResampled());
    const auto out = run(node, testSignal(kBlock * 100));
    for (float v : out) assert(std::isfinite(v));
    assert(rms(out, kBlock * 10) > 1e-4);
    std::cout << "44.1 kHz engine with a 48 kHz model: resampled, " << db(rms(out)) << " dBFS rms\n";
}

// Null test and CPU comparison against the NAM LV2 plugin, if installed.
// Only with RIGROOM_TEST_NAM pointing at a real capture: some packaged builds
// of the plugin abort on NAM's bundled example models.
void testAgainstLv2Plugin() {
    if (!std::getenv("RIGROOM_TEST_NAM")) {
        std::cout << "RIGROOM_TEST_NAM not set: skipping the null test against the NAM LV2 plugin\n";
        return;
    }
    LilvWorld* world = lilv_world_new();
    lilv_world_load_all(world);
    LilvNode* uri = lilv_new_uri(world, "http://github.com/mikeoliphant/neural-amp-modeler-lv2");
    const LilvPlugin* plugin = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(world), uri);
    if (!plugin) {
        std::cout << "NAM LV2 plugin not installed: skipping the null test\n";
        lilv_node_free(uri);
        lilv_world_free(world);
        return;
    }
    {
        LV2PluginNode lv2(world, plugin);
        lv2.prepareBlock(kRate, kBlock);
        lv2.loadModelFile(modelPath());
        // The plugin loads the model on its worker; give it time to swap in.
        const auto warm = testSignal(kBlock * 40);
        for (int i = 0; i < 50; ++i) {
            run(lv2, warm);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        CaptureNode ours;
        ours.prepareBlock(kRate, kBlock);
        assert(ours.loadModel(modelPath()));
        run(ours, warm);

        const auto signal = testSignal(static_cast<int>(kRate * 10) / kBlock * kBlock);
        double tPlugin = 0, tOurs = 0;
        const auto a = run(lv2, signal, &tPlugin);
        const auto b = run(ours, signal, &tOurs);
        std::vector<float> diff(a.size());
        for (size_t i = 0; i < a.size(); ++i) diff[i] = a[i] - b[i];
        const double level = db(rms(a)), residual = db(rms(diff));
        std::cout << "null test vs LV2 plugin: plugin " << level << " dBFS, ours " << db(rms(b))
                  << " dBFS, difference " << residual << " dBFS (" << (residual - level) << " dB below)\n";
        const double audioSeconds = signal.size() / kRate;
        std::cout << "CPU for 10 s of audio: LV2 plugin " << tPlugin * 1000 << " ms ("
                  << 100.0 * tPlugin / audioSeconds << " % of realtime), ours " << tOurs * 1000 << " ms ("
                  << 100.0 * tOurs / audioSeconds << " %)\n";
        // The plugin may apply its own level calibration; match the gain and
        // look at what is left. Same model, same core: that must be inaudible.
        double ab = 0, bb = 0;
        for (size_t i = 0; i < a.size(); ++i) { ab += double(a[i]) * b[i]; bb += double(b[i]) * b[i]; }
        const double g = bb > 0 ? ab / bb : 1.0;
        for (size_t i = 0; i < a.size(); ++i) diff[i] = a[i] - static_cast<float>(g * b[i]);
        const double matched = db(rms(diff));
        std::cout << "level-matched: plugin is " << db(g) << " dB louder; residual " << (matched - level) << " dB below\n";
        assert(matched - level < -60.0);
        // And the levels agree too (loudness normalization).
        assert(residual - level < -60.0);
    }
    lilv_node_free(uri);
    lilv_world_free(world);
}
} // namespace

int main() {
    std::cout << std::unitbuf;
    testWrapperMatchesNamCore();
    testIrConvolvesAndCabToggles();
    testResamplesForeignRate();
    testLongIrKeepsItsTail();
    testAgainstLv2Plugin();
    std::cout << "Capture tests passed" << std::endl;
    return 0;
}
