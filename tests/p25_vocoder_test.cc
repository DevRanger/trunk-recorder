// Offline regression for raw P25 mode. Run with run_p25_vocoder_test.py.
#include "trunk-recorder/config.h"
#include "trunk-recorder/call_impl.h"
#include "trunk-recorder/recorders/p25_recorder_decode.h"
#include "lib/op25_repeater/lib/p25_frame_assembler_impl.h"
#include "lib/op25_repeater/lib/op25_imbe_frame.h"
#include <gnuradio/blocks/vector_source.h>
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>

// The legacy vocoder headers define these macros; JSON fixtures need C++ bools.
#undef true
#undef false

extern std::vector<Plugin *> plugins;
namespace fs = std::filesystem;

struct Probe : Plugin_Api {
  int pcm = 0;
  int raw = 0;
  int ends = 0;
  bool retry = false;
  Call_Data_t last{};
  int audio_stream(Call *, Recorder *, int16_t *, int) override { ++pcm; return 0; }
  int voice_codec_data(Call *, int, long, uint32_t, const uint32_t *, int, int) override { ++raw; return 0; }
  int call_end(Call_Data_t info) override {
    ++ends;
    last = info;
    return retry ? 1 : 0;
  }
};

struct TestRecorder : Recorder {
  Source *source;
  explicit TestRecorder(Source *s) : Recorder(P25), source(s) {
    rec_num = 0;
    conventional = false;
    set_enable_audio_streaming(true);
  }
  Source *get_source() override { return source; }
};

struct Frames {
  std::vector<std::vector<uint32_t>> params;
  static void receive(int codec, long tg, uint32_t src, const uint32_t *p, int n, int errs, void *data) {
    assert(tg == 709 && src == 12363009);
    assert(n == (codec == 0 ? 8 : 4));
    assert(errs == 0);
    static_cast<Frames *>(data)->params.emplace_back(p, p + n);
  }
};

static void test_codecs(bool soft) {
  op25_audio audio("127.0.0.1", 0, 0);
  log_ts logger;
  auto queue = gr::msg_queue::make(100);
  std::deque<int16_t> samples;
  gr::op25_repeater::p25p1_fdma p1(audio, logger, 0, true, true, true, queue, samples, true, soft);
  Frames frames;
  p1.set_voice_codec_callback(Frames::receive, &frames);
  p1.vf_tgid = 709;
  p1.cached_src_id = 12363009;
  bit_vector ldu(1728, false);
  voice_codeword cw(voice_codeword_sz);
  imbe_header_encode(cw, 0x123, 0x456, 0x789, 0xabc, 0x123, 0x234, 0x345, 0x60);
  for (int i = 0; i < 9; ++i) imbe_interleave(ldu, cw, i);

  // Both vocoder implementations must preserve raw frames and timing on reuse.
  assert(p1.d_vocoder_enabled);
  std::vector<std::vector<uint32_t>> expected;
  for (bool enabled : {true, false, true}) {
    p1.set_vocoder_enabled(enabled);
    p1.reset_ess();
    frames.params.clear();
    samples.clear();
    p1.process_voice(ldu, FT_LDU1);
    assert(samples.size() == 9 * 160 && frames.params.size() == 9);
    if (expected.empty()) expected = frames.params;
    assert(frames.params == expected);
    if (!enabled) assert(std::all_of(samples.begin(), samples.end(), [](int16_t s) { return s == 0; }));
    else assert(std::any_of(samples.begin(), samples.end(), [](int16_t s) { return s != 0; }));
  }
  // Disabling synthesis must not lose encrypted-call notifications.
  p1.set_vocoder_enabled(false);
  p1.ess_algid = 0x84;
  samples.clear();
  p1.process_voice(ldu, FT_LDU2);
  assert(samples.size() == 9 * 160 && queue->count() > 0);

  p25p2_tdma p2(audio, logger, 0, 0, true, queue, samples, true, soft);
  p2.set_voice_codec_callback(Frames::receive, &frames);
  p2.grp_id = 709;
  p2.cached_src_id = 12363009;
  uint8_t dibits[36] = {};
  int b[9] = {40, 10, 10, 100, 50, 12, 7, 7, 3};
  p25p2_vf vf;
  vf.encode_vcw(dibits, b);
  expected.clear();
  assert(p2.d_vocoder_enabled);
  for (bool enabled : {true, false, true}) {
    p2.set_vocoder_enabled(enabled);
    p2.reset_ess();
    frames.params.clear();
    samples.clear();
    p2.handle_voice_frame(dibits, 0, 0);
    assert(samples.size() == 160 && frames.params.size() == 1);
    if (expected.empty()) expected = frames.params;
    assert(frames.params == expected);
    if (!enabled) assert(std::all_of(samples.begin(), samples.end(), [](int16_t s) { return s == 0; }));
    else assert(std::any_of(samples.begin(), samples.end(), [](int16_t s) { return s != 0; }));
  }
  p2.set_vocoder_enabled(false);
  p2.ess_algid = 0x84;
  samples.clear();
  p2.handle_voice_frame(dibits, 0, 0);
  assert(samples.size() == 160);
}

static System *read_system(const fs::path &dir, const json &setting, bool present, const std::string &type = "p25") {
  json system = {{"type", type}, {"shortName", "test"}, {"control_channels", {851000000}}, {"channels", {851000000}}};
  if (present) system["p25Vocoder"] = setting;
  json data = {{"ver", 2}, {"consoleLog", false}, {"logLevel", "error"}, {"sources", json::array()}, {"systems", {system}},
               {"captureDir", dir.string()}, {"tempDir", dir.string()}};
  const auto path = dir / "config.json";
  std::ofstream(path) << data.dump();
  Config config{};
  auto tb = gr::make_top_block("config test");
  std::vector<Source *> sources;
  std::vector<System *> systems;
  if (!load_config(path.string(), config, tb, sources, systems)) return nullptr;
  assert(systems.size() == 1 && sources.empty());
  return systems[0];
}

static void test_recorder(const fs::path &dir) {
  auto omitted = read_system(dir, true, false);
  assert(omitted && omitted->get_p25_vocoder());
  auto enabled = read_system(dir, true, true);
  assert(enabled && enabled->get_p25_vocoder());
  auto conventional = read_system(dir, false, true, "conventionalP25");
  assert(conventional && !conventional->get_p25_vocoder());
  assert(read_system(dir, false, true, "conventional") == nullptr);
  assert(read_system(dir, "false", true) == nullptr);
  System *sys = read_system(dir, false, true);
  assert(sys && !sys->get_p25_vocoder());
  sys->set_call_log(false);
  sys->set_min_tx_duration(0.8);
  sys->set_min_duration(1.0);
  // These deliberately remain enabled: raw mode must override audio processing.
  sys->set_audio_archive(true);
  sys->set_transmission_archive(true);
  sys->set_upload_script("/bin/false");

  Config config{};
  config.temp_dir = (dir / "transmissions").string();
  config.capture_dir = (dir / "calls").string();
  const auto iq = dir / "empty.iq";
  const std::vector<char> iq_data(4096, 0);
  std::ofstream(iq, std::ios::binary).write(iq_data.data(), iq_data.size());
  Source source(iq.string(), false, 851000000, 2400000, &config);
  TestRecorder recorder(&source);
  Call_impl call(709, 851000000, sys, config);
  call.set_recorder(&recorder);
  call.curr_src_id = 12363009;
  call.set_state(RECORDING);
  auto probe = boost::shared_ptr<Probe>(new Probe());
  Plugin plugin{};
  plugin.api = probe;
  plugin.state = PLUGIN_RUNNING;
  plugin.name = "probe";
  plugins.push_back(&plugin);
  auto decoder = make_p25_recorder_decode(&recorder, 0, false);

  // Reusing the very same recorder must restore PCM callbacks and both decoders.
  int16_t pcm[160] = {};
  uint32_t raw[8] = {};
  for (bool enabled : {false, true, false}) {
    sys->set_p25_vocoder(enabled);
    decoder->start(&call);
    auto assembler = std::dynamic_pointer_cast<gr::op25_repeater::p25_frame_assembler_impl>(decoder->get_transmission_sink());
    assert(assembler->p1fdma.d_vocoder_enabled == enabled && assembler->p2tdma.d_vocoder_enabled == enabled);
    int before = probe->pcm;
    decoder->plugin_callback_handler(pcm, 160);
    assert(probe->pcm == before + (enabled ? 1 : 0));
    decoder->voice_codec_cb_handler(0, 709, 12363009, raw, 8, 0, decoder.get());
    decoder->stop();
  }
  assert(probe->raw == 3);

  // Use actual GNU Radio tags to finish a 20 ms transmission in metadata-only mode.
  auto sink = gr::blocks::transmission_sink::make(1, 8000, 16);
  sink->set_record_audio(false);
  sink->start_recording(&call);
  std::vector<gr::tag_t> tags;
  for (const auto &entry : std::vector<std::pair<std::string, long>>{{"grp_id", 709}, {"src_id", 12363009}}) {
    gr::tag_t tag{};
    tag.offset = 0;
    tag.key = pmt::intern(entry.first);
    tag.value = pmt::from_long(entry.second);
    tag.srcid = pmt::PMT_NIL;
    tags.push_back(tag);
  }
  gr::tag_t end{};
  end.offset = 160;
  end.key = pmt::intern("terminate");
  end.value = pmt::from_long(1);
  end.srcid = pmt::PMT_NIL;
  tags.push_back(end);
  auto input = gr::blocks::vector_source_s::make(std::vector<int16_t>(161, 0), false, 1, tags);
  auto tb = gr::make_top_block("timing tags test");
  sink->set_max_noutput_items(160);
  tb->connect(input, 0, sink, 0);
  tb->run();
  assert(sink->get_state() == IDLE);
  assert(sink->total_length_in_seconds() == 0.02);
  assert(sink->get_last_write_time().time_since_epoch().count() > 0);
  sink->stop_recording();
  call.transmission_list = sink->get_transmission_list();
  assert(call.transmission_list.size() == 1);
  assert(call.transmission_list[0].filename.empty());
  assert(call.transmission_list[0].source == 12363009);
  assert(!fs::exists(config.temp_dir));

  auto info = Call_Concluder::create_call_data(&call, sys, config);
  assert(info.filename.empty() && info.converted.empty() && info.raw_audio_filename.empty());
  assert(info.status_filename.empty() && info.transmission_list.size() == 1);
  assert(info.call_length_ms == 20 && info.min_transmissions_removed == 0);
  assert(!fs::exists(config.capture_dir));
  auto result = upload_call_worker(info);
  assert(result.status == SUCCESS && probe->ends == 1);
  assert(probe->last.call_json["audio_available"] == false);

  // Retry handling still works without ffmpeg, files, or the upload script.
  probe->retry = true;
  result = upload_call_worker(info);
  assert(result.status == RETRY);
  probe->retry = false;
  result = upload_call_worker(result);
  assert(result.status == SUCCESS && probe->ends == 3);

  // File-duration limits must not withhold the end of a streamed short call.
  Call_Concluder::conclude_call(&call, sys, config);
  assert(Call_Concluder::shutdown_call_data_workers(std::chrono::seconds(5)));
  assert(probe->ends == 4 && probe->last.call_length_ms == 20);

  // Even empty/encrypted raw calls must finish through the normal async worker.
  call.transmission_list.clear();
  call.set_encrypted(true);
  Call_Concluder::conclude_call(&call, sys, config);
  assert(Call_Concluder::shutdown_call_data_workers(std::chrono::seconds(5)));
  assert(probe->ends == 5);
  sys->set_call_log(true);
  info = Call_Concluder::create_call_data(&call, sys, config);
  result = upload_call_worker(info);
  assert(result.status == SUCCESS && fs::exists(info.status_filename));

  // A failed optional JSON write must not strand the raw plugin's active call.
  info.status_filename = (dir / "missing-parent" / "call.json").string();
  result = upload_call_worker(info);
  assert(result.status == SUCCESS && probe->ends == 7);

  // The normal recording mode retains its existing short-transmission filter.
  call.transmission_list = sink->get_transmission_list();
  sys->set_p25_vocoder(true);
  info = Call_Concluder::create_call_data(&call, sys, config);
  assert(!info.filename.empty() && info.transmission_list.empty() && info.min_transmissions_removed == 1);

  // A reused sink must create real WAVs again when PCM recording is restored.
  sink->set_record_audio(true);
  sink->start_recording(&call);
  std::vector<int16_t> data(160, 123);
  gr_vector_const_void_star inputs{data.data()};
  gr_vector_void_star outputs;
  sink->dowork(160, inputs, outputs);
  sink->stop_recording();
  auto tx = sink->get_transmission_list();
  assert(tx.size() == 1 && fs::file_size(tx[0].filename) > 320);
  plugins.clear();
}

int main(int argc, char **argv) {
  assert(argc == 2);
  test_codecs(false);
  test_codecs(true);
  test_recorder(argv[1]);
  std::cout << "PASS: P25 codecs, raw callbacks, config, recorder reuse, timing tags, files, and call-end lifecycle\n";
}
