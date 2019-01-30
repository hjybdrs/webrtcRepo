/*
 *  Copyright 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/receive_statistics_proxy.h"

#include <limits>
#include <memory>
#include <string>

#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_rotation.h"
#include "system_wrappers/include/metrics.h"
#include "test/gtest.h"

namespace webrtc {
namespace {
const int64_t kFreqOffsetProcessIntervalInMs = 40000;
const uint32_t kLocalSsrc = 123;
const uint32_t kRemoteSsrc = 456;
const int kMinRequiredSamples = 200;
const int kWidth = 1280;
const int kHeight = 720;

}  // namespace

// TODO(sakal): ReceiveStatisticsProxy is lacking unittesting.
class ReceiveStatisticsProxyTest
    : public ::testing::TestWithParam<webrtc::VideoContentType> {
 public:
  ReceiveStatisticsProxyTest() : fake_clock_(1234), config_(GetTestConfig()) {}
  virtual ~ReceiveStatisticsProxyTest() {}

 protected:
  virtual void SetUp() {
    metrics::Reset();
    statistics_proxy_.reset(new ReceiveStatisticsProxy(&config_, &fake_clock_));
  }

  VideoReceiveStream::Config GetTestConfig() {
    VideoReceiveStream::Config config(nullptr);
    config.rtp.local_ssrc = kLocalSsrc;
    config.rtp.remote_ssrc = kRemoteSsrc;
    return config;
  }

  void InsertFirstRtpPacket(uint32_t ssrc) {
    StreamDataCounters counters;
    counters.first_packet_time_ms = fake_clock_.TimeInMilliseconds();
    statistics_proxy_->DataCountersUpdated(counters, ssrc);
  }

  VideoFrame CreateFrame(int width, int height) {
    return CreateVideoFrame(width, height, 0);
  }

  VideoFrame CreateFrameWithRenderTimeMs(int64_t render_time_ms) {
    return CreateVideoFrame(kWidth, kHeight, render_time_ms);
  }

  VideoFrame CreateVideoFrame(int width, int height, int64_t render_time_ms) {
    VideoFrame frame =
        VideoFrame::Builder()
            .set_video_frame_buffer(I420Buffer::Create(width, height))
            .set_timestamp_rtp(0)
            .set_timestamp_ms(render_time_ms)
            .set_rotation(kVideoRotation_0)
            .build();
    frame.set_ntp_time_ms(fake_clock_.CurrentNtpInMilliseconds());
    return frame;
  }

  SimulatedClock fake_clock_;
  const VideoReceiveStream::Config config_;
  std::unique_ptr<ReceiveStatisticsProxy> statistics_proxy_;
};

TEST_F(ReceiveStatisticsProxyTest, OnDecodedFrameIncreasesFramesDecoded) {
  EXPECT_EQ(0u, statistics_proxy_->GetStats().frames_decoded);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  for (uint32_t i = 1; i <= 3; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                      VideoContentType::UNSPECIFIED);
    EXPECT_EQ(i, statistics_proxy_->GetStats().frames_decoded);
  }
}

TEST_F(ReceiveStatisticsProxyTest, DecodedFpsIsReported) {
  const int kFps = 20;
  const int kRequiredSamples = metrics::kMinRunTimeInSeconds * kFps;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  for (int i = 0; i < kRequiredSamples; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                      VideoContentType::UNSPECIFIED);
    fake_clock_.AdvanceTimeMilliseconds(1000 / kFps);
  }
  statistics_proxy_.reset();
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.DecodedFramesPerSecond"));
  EXPECT_EQ(1, metrics::NumEvents("WebRTC.Video.DecodedFramesPerSecond", kFps));
}

TEST_F(ReceiveStatisticsProxyTest, DecodedFpsIsNotReportedForTooFewSamples) {
  const int kFps = 20;
  const int kRequiredSamples = metrics::kMinRunTimeInSeconds * kFps;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  for (int i = 0; i < kRequiredSamples - 1; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                      VideoContentType::UNSPECIFIED);
    fake_clock_.AdvanceTimeMilliseconds(1000 / kFps);
  }
  statistics_proxy_.reset();
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.DecodedFramesPerSecond"));
}

TEST_F(ReceiveStatisticsProxyTest, DecodedFpsIsReportedWithQpReset) {
  const int kFps1 = 10;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  for (int i = 0; i < metrics::kMinRunTimeInSeconds * kFps1; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                      VideoContentType::UNSPECIFIED);
    fake_clock_.AdvanceTimeMilliseconds(1000 / kFps1);
  }
  // First QP value received, resets frames decoded.
  const int kFps2 = 20;
  for (int i = 0; i < metrics::kMinRunTimeInSeconds * kFps2; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, 1u, VideoContentType::UNSPECIFIED);
    fake_clock_.AdvanceTimeMilliseconds(1000 / kFps2);
  }
  statistics_proxy_.reset();
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.DecodedFramesPerSecond"));
  EXPECT_EQ(1,
            metrics::NumEvents("WebRTC.Video.DecodedFramesPerSecond", kFps2));
}

TEST_F(ReceiveStatisticsProxyTest, OnDecodedFrameWithQpResetsFramesDecoded) {
  EXPECT_EQ(0u, statistics_proxy_->GetStats().frames_decoded);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  for (uint32_t i = 1; i <= 3; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                      VideoContentType::UNSPECIFIED);
    EXPECT_EQ(i, statistics_proxy_->GetStats().frames_decoded);
  }
  statistics_proxy_->OnDecodedFrame(frame, 1u, VideoContentType::UNSPECIFIED);
  EXPECT_EQ(1u, statistics_proxy_->GetStats().frames_decoded);
}

TEST_F(ReceiveStatisticsProxyTest, OnDecodedFrameIncreasesQpSum) {
  EXPECT_EQ(absl::nullopt, statistics_proxy_->GetStats().qp_sum);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  statistics_proxy_->OnDecodedFrame(frame, 3u, VideoContentType::UNSPECIFIED);
  EXPECT_EQ(3u, statistics_proxy_->GetStats().qp_sum);
  statistics_proxy_->OnDecodedFrame(frame, 127u, VideoContentType::UNSPECIFIED);
  EXPECT_EQ(130u, statistics_proxy_->GetStats().qp_sum);
}

TEST_F(ReceiveStatisticsProxyTest, ReportsContentType) {
  const std::string kRealtimeString("realtime");
  const std::string kScreenshareString("screen");
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  EXPECT_EQ(kRealtimeString, videocontenttypehelpers::ToString(
                            statistics_proxy_->GetStats().content_type));
  statistics_proxy_->OnDecodedFrame(frame, 3u, VideoContentType::SCREENSHARE);
  EXPECT_EQ(kScreenshareString, videocontenttypehelpers::ToString(
                          statistics_proxy_->GetStats().content_type));
  statistics_proxy_->OnDecodedFrame(frame, 3u, VideoContentType::UNSPECIFIED);
  EXPECT_EQ(kRealtimeString, videocontenttypehelpers::ToString(
                            statistics_proxy_->GetStats().content_type));
}

TEST_F(ReceiveStatisticsProxyTest, ReportsMaxInterframeDelay) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  const int64_t kInterframeDelayMs1 = 100;
  const int64_t kInterframeDelayMs2 = 200;
  const int64_t kInterframeDelayMs3 = 100;
  EXPECT_EQ(-1, statistics_proxy_->GetStats().interframe_delay_max_ms);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                    VideoContentType::UNSPECIFIED);
  EXPECT_EQ(-1, statistics_proxy_->GetStats().interframe_delay_max_ms);

  fake_clock_.AdvanceTimeMilliseconds(kInterframeDelayMs1);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                    VideoContentType::UNSPECIFIED);
  EXPECT_EQ(kInterframeDelayMs1,
            statistics_proxy_->GetStats().interframe_delay_max_ms);

  fake_clock_.AdvanceTimeMilliseconds(kInterframeDelayMs2);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                    VideoContentType::UNSPECIFIED);
  EXPECT_EQ(kInterframeDelayMs2,
            statistics_proxy_->GetStats().interframe_delay_max_ms);

  fake_clock_.AdvanceTimeMilliseconds(kInterframeDelayMs3);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                    VideoContentType::UNSPECIFIED);
  // kInterframeDelayMs3 is smaller than kInterframeDelayMs2.
  EXPECT_EQ(kInterframeDelayMs2,
            statistics_proxy_->GetStats().interframe_delay_max_ms);
}

TEST_F(ReceiveStatisticsProxyTest, ReportInterframeDelayInWindow) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  const int64_t kInterframeDelayMs1 = 900;
  const int64_t kInterframeDelayMs2 = 750;
  const int64_t kInterframeDelayMs3 = 700;
  EXPECT_EQ(-1, statistics_proxy_->GetStats().interframe_delay_max_ms);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                    VideoContentType::UNSPECIFIED);
  EXPECT_EQ(-1, statistics_proxy_->GetStats().interframe_delay_max_ms);

  fake_clock_.AdvanceTimeMilliseconds(kInterframeDelayMs1);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                    VideoContentType::UNSPECIFIED);
  EXPECT_EQ(kInterframeDelayMs1,
            statistics_proxy_->GetStats().interframe_delay_max_ms);

  fake_clock_.AdvanceTimeMilliseconds(kInterframeDelayMs2);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                    VideoContentType::UNSPECIFIED);
  // Still first delay is the maximum
  EXPECT_EQ(kInterframeDelayMs1,
            statistics_proxy_->GetStats().interframe_delay_max_ms);

  fake_clock_.AdvanceTimeMilliseconds(kInterframeDelayMs3);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                    VideoContentType::UNSPECIFIED);
  // Now the first sample is out of the window, so the second is the maximum.
  EXPECT_EQ(kInterframeDelayMs2,
            statistics_proxy_->GetStats().interframe_delay_max_ms);
}

TEST_F(ReceiveStatisticsProxyTest, OnDecodedFrameWithoutQpQpSumWontExist) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  EXPECT_EQ(absl::nullopt, statistics_proxy_->GetStats().qp_sum);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                    VideoContentType::UNSPECIFIED);
  EXPECT_EQ(absl::nullopt, statistics_proxy_->GetStats().qp_sum);
}

TEST_F(ReceiveStatisticsProxyTest, OnDecodedFrameWithoutQpResetsQpSum) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  EXPECT_EQ(absl::nullopt, statistics_proxy_->GetStats().qp_sum);
  statistics_proxy_->OnDecodedFrame(frame, 3u, VideoContentType::UNSPECIFIED);
  EXPECT_EQ(3u, statistics_proxy_->GetStats().qp_sum);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                    VideoContentType::UNSPECIFIED);
  EXPECT_EQ(absl::nullopt, statistics_proxy_->GetStats().qp_sum);
}

TEST_F(ReceiveStatisticsProxyTest, OnRenderedFrameIncreasesFramesRendered) {
  EXPECT_EQ(0u, statistics_proxy_->GetStats().frames_rendered);
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  for (uint32_t i = 1; i <= 3; ++i) {
    statistics_proxy_->OnRenderedFrame(frame);
    EXPECT_EQ(i, statistics_proxy_->GetStats().frames_rendered);
  }
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsSsrc) {
  EXPECT_EQ(kRemoteSsrc, statistics_proxy_->GetStats().ssrc);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsIncomingPayloadType) {
  const int kPayloadType = 111;
  statistics_proxy_->OnIncomingPayloadType(kPayloadType);
  EXPECT_EQ(kPayloadType, statistics_proxy_->GetStats().current_payload_type);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsDecoderImplementationName) {
  const char* kName = "decoderName";
  statistics_proxy_->OnDecoderImplementationName(kName);
  EXPECT_STREQ(
      kName, statistics_proxy_->GetStats().decoder_implementation_name.c_str());
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsOnCompleteFrame) {
  const int kFrameSizeBytes = 1000;
  statistics_proxy_->OnCompleteFrame(true, kFrameSizeBytes,
                                     VideoContentType::UNSPECIFIED);
  VideoReceiveStream::Stats stats = statistics_proxy_->GetStats();
  EXPECT_EQ(1, stats.network_frame_rate);
  EXPECT_EQ(1, stats.frame_counts.key_frames);
  EXPECT_EQ(0, stats.frame_counts.delta_frames);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsDecodeTimingStats) {
  const int kDecodeMs = 1;
  const int kMaxDecodeMs = 2;
  const int kCurrentDelayMs = 3;
  const int kTargetDelayMs = 4;
  const int kJitterBufferMs = 5;
  const int kMinPlayoutDelayMs = 6;
  const int kRenderDelayMs = 7;
  const int64_t kRttMs = 8;
  statistics_proxy_->OnRttUpdate(kRttMs, 0);
  statistics_proxy_->OnFrameBufferTimingsUpdated(
      kDecodeMs, kMaxDecodeMs, kCurrentDelayMs, kTargetDelayMs, kJitterBufferMs,
      kMinPlayoutDelayMs, kRenderDelayMs);
  VideoReceiveStream::Stats stats = statistics_proxy_->GetStats();
  EXPECT_EQ(kDecodeMs, stats.decode_ms);
  EXPECT_EQ(kMaxDecodeMs, stats.max_decode_ms);
  EXPECT_EQ(kCurrentDelayMs, stats.current_delay_ms);
  EXPECT_EQ(kTargetDelayMs, stats.target_delay_ms);
  EXPECT_EQ(kJitterBufferMs, stats.jitter_buffer_ms);
  EXPECT_EQ(kMinPlayoutDelayMs, stats.min_playout_delay_ms);
  EXPECT_EQ(kRenderDelayMs, stats.render_delay_ms);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsRtcpPacketTypeCounts) {
  const uint32_t kFirPackets = 33;
  const uint32_t kPliPackets = 44;
  const uint32_t kNackPackets = 55;
  RtcpPacketTypeCounter counter;
  counter.fir_packets = kFirPackets;
  counter.pli_packets = kPliPackets;
  counter.nack_packets = kNackPackets;
  statistics_proxy_->RtcpPacketTypesCounterUpdated(kRemoteSsrc, counter);
  VideoReceiveStream::Stats stats = statistics_proxy_->GetStats();
  EXPECT_EQ(kFirPackets, stats.rtcp_packet_type_counts.fir_packets);
  EXPECT_EQ(kPliPackets, stats.rtcp_packet_type_counts.pli_packets);
  EXPECT_EQ(kNackPackets, stats.rtcp_packet_type_counts.nack_packets);
}

TEST_F(ReceiveStatisticsProxyTest,
       GetStatsReportsNoRtcpPacketTypeCountsForUnknownSsrc) {
  RtcpPacketTypeCounter counter;
  counter.fir_packets = 33;
  statistics_proxy_->RtcpPacketTypesCounterUpdated(kRemoteSsrc + 1, counter);
  EXPECT_EQ(0u,
            statistics_proxy_->GetStats().rtcp_packet_type_counts.fir_packets);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsFrameCounts) {
  const int kKeyFrames = 3;
  const int kDeltaFrames = 22;
  FrameCounts frame_counts;
  frame_counts.key_frames = kKeyFrames;
  frame_counts.delta_frames = kDeltaFrames;
  statistics_proxy_->OnFrameCountsUpdated(frame_counts);
  VideoReceiveStream::Stats stats = statistics_proxy_->GetStats();
  EXPECT_EQ(kKeyFrames, stats.frame_counts.key_frames);
  EXPECT_EQ(kDeltaFrames, stats.frame_counts.delta_frames);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsDiscardedPackets) {
  const int kDiscardedPackets = 12;
  statistics_proxy_->OnDiscardedPacketsUpdated(kDiscardedPackets);
  EXPECT_EQ(kDiscardedPackets, statistics_proxy_->GetStats().discarded_packets);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsRtcpStats) {
  const uint8_t kFracLost = 0;
  const int32_t kCumLost = 1;
  const uint32_t kExtSeqNum = 10;
  const uint32_t kJitter = 4;

  RtcpStatistics rtcp_stats;
  rtcp_stats.fraction_lost = kFracLost;
  rtcp_stats.packets_lost = kCumLost;
  rtcp_stats.extended_highest_sequence_number = kExtSeqNum;
  rtcp_stats.jitter = kJitter;
  statistics_proxy_->StatisticsUpdated(rtcp_stats, kRemoteSsrc);

  VideoReceiveStream::Stats stats = statistics_proxy_->GetStats();
  EXPECT_EQ(kFracLost, stats.rtcp_stats.fraction_lost);
  EXPECT_EQ(kCumLost, stats.rtcp_stats.packets_lost);
  EXPECT_EQ(kExtSeqNum, stats.rtcp_stats.extended_highest_sequence_number);
  EXPECT_EQ(kJitter, stats.rtcp_stats.jitter);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsCName) {
  const char* kName = "cName";
  statistics_proxy_->CNameChanged(kName, kRemoteSsrc);
  EXPECT_STREQ(kName, statistics_proxy_->GetStats().c_name.c_str());
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsNoCNameForUnknownSsrc) {
  const char* kName = "cName";
  statistics_proxy_->CNameChanged(kName, kRemoteSsrc + 1);
  EXPECT_STREQ("", statistics_proxy_->GetStats().c_name.c_str());
}

TEST_F(ReceiveStatisticsProxyTest,
       ReportsLongestTimingFrameInfo) {
  const int64_t kShortEndToEndDelay = 10;
  const int64_t kMedEndToEndDelay = 20;
  const int64_t kLongEndToEndDelay = 100;
  const uint32_t kExpectedRtpTimestamp = 2;
  TimingFrameInfo info;
  absl::optional<TimingFrameInfo> result;
  info.rtp_timestamp = kExpectedRtpTimestamp - 1;
  info.capture_time_ms = 0;
  info.decode_finish_ms = kShortEndToEndDelay;
  statistics_proxy_->OnTimingFrameInfoUpdated(info);
  info.rtp_timestamp =
      kExpectedRtpTimestamp;  // this frame should be reported in the end.
  info.capture_time_ms = 0;
  info.decode_finish_ms = kLongEndToEndDelay;
  statistics_proxy_->OnTimingFrameInfoUpdated(info);
  info.rtp_timestamp = kExpectedRtpTimestamp + 1;
  info.capture_time_ms = 0;
  info.decode_finish_ms = kMedEndToEndDelay;
  statistics_proxy_->OnTimingFrameInfoUpdated(info);
  result = statistics_proxy_->GetStats().timing_frame_info;
  EXPECT_TRUE(result);
  EXPECT_EQ(kExpectedRtpTimestamp, result->rtp_timestamp);
}

TEST_F(ReceiveStatisticsProxyTest, RespectsReportingIntervalForTimingFrames) {
  TimingFrameInfo info;
  const int64_t kShortEndToEndDelay = 10;
  const uint32_t kExpectedRtpTimestamp = 2;
  const int64_t kShortDelayMs = 1000;
  const int64_t kLongDelayMs = 10000;
  absl::optional<TimingFrameInfo> result;
  info.rtp_timestamp = kExpectedRtpTimestamp;
  info.capture_time_ms = 0;
  info.decode_finish_ms = kShortEndToEndDelay;
  statistics_proxy_->OnTimingFrameInfoUpdated(info);
  fake_clock_.AdvanceTimeMilliseconds(kShortDelayMs);
  result = statistics_proxy_->GetStats().timing_frame_info;
  EXPECT_TRUE(result);
  EXPECT_EQ(kExpectedRtpTimestamp, result->rtp_timestamp);
  fake_clock_.AdvanceTimeMilliseconds(kLongDelayMs);
  result = statistics_proxy_->GetStats().timing_frame_info;
  EXPECT_FALSE(result);
}

TEST_F(ReceiveStatisticsProxyTest, LifetimeHistogramIsUpdated) {
  const int64_t kTimeSec = 3;
  fake_clock_.AdvanceTimeMilliseconds(kTimeSec * 1000);
  // Need at least one frame to report stream lifetime.
  statistics_proxy_->OnCompleteFrame(true, 1000, VideoContentType::UNSPECIFIED);
  // Histograms are updated when the statistics_proxy_ is deleted.
  statistics_proxy_.reset();
  EXPECT_EQ(1,
            metrics::NumSamples("WebRTC.Video.ReceiveStreamLifetimeInSeconds"));
  EXPECT_EQ(1, metrics::NumEvents("WebRTC.Video.ReceiveStreamLifetimeInSeconds",
                                  kTimeSec));
}

TEST_F(ReceiveStatisticsProxyTest,
       LifetimeHistogramNotReportedForEmptyStreams) {
  const int64_t kTimeSec = 3;
  fake_clock_.AdvanceTimeMilliseconds(kTimeSec * 1000);
  // No frames received.
  // Histograms are updated when the statistics_proxy_ is deleted.
  statistics_proxy_.reset();
  EXPECT_EQ(0,
            metrics::NumSamples("WebRTC.Video.ReceiveStreamLifetimeInSeconds"));
}

TEST_F(ReceiveStatisticsProxyTest, BadCallHistogramsAreUpdated) {
  // Based on the tuning parameters this will produce 7 uncertain states,
  // then 10 certainly bad states. There has to be 10 certain states before
  // any histograms are recorded.
  const int kNumBadSamples = 17;

  StreamDataCounters counters;
  counters.first_packet_time_ms = fake_clock_.TimeInMilliseconds();
  statistics_proxy_->DataCountersUpdated(counters, config_.rtp.remote_ssrc);

  for (int i = 0; i < kNumBadSamples; ++i) {
    // Since OnRenderedFrame is never called the fps in each sample will be 0,
    // i.e. bad
    fake_clock_.AdvanceTimeMilliseconds(1000);
    statistics_proxy_->OnIncomingRate(0, 0);
  }
  // Histograms are updated when the statistics_proxy_ is deleted.
  statistics_proxy_.reset();
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.BadCall.Any"));
  EXPECT_EQ(1, metrics::NumEvents("WebRTC.Video.BadCall.Any", 100));

  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.BadCall.FrameRate"));
  EXPECT_EQ(1, metrics::NumEvents("WebRTC.Video.BadCall.FrameRate", 100));

  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.BadCall.FrameRateVariance"));

  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.BadCall.Qp"));
}

TEST_F(ReceiveStatisticsProxyTest, PacketLossHistogramIsUpdated) {
  const uint32_t kCumLost1 = 1;
  const uint32_t kExtSeqNum1 = 10;
  const uint32_t kCumLost2 = 2;
  const uint32_t kExtSeqNum2 = 20;

  // One report block received.
  RtcpStatistics rtcp_stats1;
  rtcp_stats1.packets_lost = kCumLost1;
  rtcp_stats1.extended_highest_sequence_number = kExtSeqNum1;
  statistics_proxy_->StatisticsUpdated(rtcp_stats1, kRemoteSsrc);

  // Two report blocks received.
  RtcpStatistics rtcp_stats2;
  rtcp_stats2.packets_lost = kCumLost2;
  rtcp_stats2.extended_highest_sequence_number = kExtSeqNum2;
  statistics_proxy_->StatisticsUpdated(rtcp_stats2, kRemoteSsrc);

  // Two received report blocks but min run time has not passed.
  fake_clock_.AdvanceTimeMilliseconds(metrics::kMinRunTimeInSeconds * 1000 - 1);
  SetUp();  // Reset stat proxy causes histograms to be updated.
  EXPECT_EQ(0,
            metrics::NumSamples("WebRTC.Video.ReceivedPacketsLostInPercent"));

  // Two report blocks received.
  statistics_proxy_->StatisticsUpdated(rtcp_stats1, kRemoteSsrc);
  statistics_proxy_->StatisticsUpdated(rtcp_stats2, kRemoteSsrc);

  // Two received report blocks and min run time has passed.
  fake_clock_.AdvanceTimeMilliseconds(metrics::kMinRunTimeInSeconds * 1000);
  SetUp();
  EXPECT_EQ(1,
            metrics::NumSamples("WebRTC.Video.ReceivedPacketsLostInPercent"));
  EXPECT_EQ(1, metrics::NumEvents("WebRTC.Video.ReceivedPacketsLostInPercent",
                                  (kCumLost2 - kCumLost1) * 100 /
                                      (kExtSeqNum2 - kExtSeqNum1)));
}

TEST_F(ReceiveStatisticsProxyTest,
       PacketLossHistogramIsNotUpdatedIfLessThanTwoReportBlocksAreReceived) {
  RtcpStatistics rtcp_stats1;
  rtcp_stats1.packets_lost = 1;
  rtcp_stats1.extended_highest_sequence_number = 10;

  // Min run time has passed but no received report block.
  fake_clock_.AdvanceTimeMilliseconds(metrics::kMinRunTimeInSeconds * 1000);
  SetUp();  // Reset stat proxy causes histograms to be updated.
  EXPECT_EQ(0,
            metrics::NumSamples("WebRTC.Video.ReceivedPacketsLostInPercent"));

  // Min run time has passed but only one received report block.
  statistics_proxy_->StatisticsUpdated(rtcp_stats1, kRemoteSsrc);
  fake_clock_.AdvanceTimeMilliseconds(metrics::kMinRunTimeInSeconds * 1000);
  SetUp();
  EXPECT_EQ(0,
            metrics::NumSamples("WebRTC.Video.ReceivedPacketsLostInPercent"));
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsAvSyncOffset) {
  const int64_t kSyncOffsetMs = 22;
  const double kFreqKhz = 90.0;
  EXPECT_EQ(std::numeric_limits<int>::max(),
            statistics_proxy_->GetStats().sync_offset_ms);
  statistics_proxy_->OnSyncOffsetUpdated(kSyncOffsetMs, kFreqKhz);
  EXPECT_EQ(kSyncOffsetMs, statistics_proxy_->GetStats().sync_offset_ms);
}

TEST_F(ReceiveStatisticsProxyTest, AvSyncOffsetHistogramIsUpdated) {
  const int64_t kSyncOffsetMs = 22;
  const double kFreqKhz = 90.0;
  for (int i = 0; i < kMinRequiredSamples; ++i)
    statistics_proxy_->OnSyncOffsetUpdated(kSyncOffsetMs, kFreqKhz);
  // Histograms are updated when the statistics_proxy_ is deleted.
  statistics_proxy_.reset();
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.AVSyncOffsetInMs"));
  EXPECT_EQ(1,
            metrics::NumEvents("WebRTC.Video.AVSyncOffsetInMs", kSyncOffsetMs));
}

TEST_F(ReceiveStatisticsProxyTest, RtpToNtpFrequencyOffsetHistogramIsUpdated) {
  const int64_t kSyncOffsetMs = 22;
  const double kFreqKhz = 90.0;
  statistics_proxy_->OnSyncOffsetUpdated(kSyncOffsetMs, kFreqKhz);
  statistics_proxy_->OnSyncOffsetUpdated(kSyncOffsetMs, kFreqKhz + 2.2);
  fake_clock_.AdvanceTimeMilliseconds(kFreqOffsetProcessIntervalInMs);
  // Process interval passed, max diff: 2.
  statistics_proxy_->OnSyncOffsetUpdated(kSyncOffsetMs, kFreqKhz + 1.1);
  statistics_proxy_->OnSyncOffsetUpdated(kSyncOffsetMs, kFreqKhz - 4.2);
  statistics_proxy_->OnSyncOffsetUpdated(kSyncOffsetMs, kFreqKhz - 0.9);
  fake_clock_.AdvanceTimeMilliseconds(kFreqOffsetProcessIntervalInMs);
  // Process interval passed, max diff: 4.
  statistics_proxy_->OnSyncOffsetUpdated(kSyncOffsetMs, kFreqKhz);
  statistics_proxy_.reset();
  // Average reported: (2 + 4) / 2 = 3.
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.RtpToNtpFreqOffsetInKhz"));
  EXPECT_EQ(1, metrics::NumEvents("WebRTC.Video.RtpToNtpFreqOffsetInKhz", 3));
}

TEST_F(ReceiveStatisticsProxyTest, Vp8QpHistogramIsUpdated) {
  const int kQp = 22;

  for (int i = 0; i < kMinRequiredSamples; ++i)
    statistics_proxy_->OnPreDecode(kVideoCodecVP8, kQp);

  statistics_proxy_.reset();
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.Decoded.Vp8.Qp"));
  EXPECT_EQ(1, metrics::NumEvents("WebRTC.Video.Decoded.Vp8.Qp", kQp));
}

TEST_F(ReceiveStatisticsProxyTest, Vp8QpHistogramIsNotUpdatedForTooFewSamples) {
  const int kQp = 22;

  for (int i = 0; i < kMinRequiredSamples - 1; ++i)
    statistics_proxy_->OnPreDecode(kVideoCodecVP8, kQp);

  statistics_proxy_.reset();
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.Decoded.Vp8.Qp"));
}

TEST_F(ReceiveStatisticsProxyTest, Vp8QpHistogramIsNotUpdatedIfNoQpValue) {
  for (int i = 0; i < kMinRequiredSamples; ++i)
    statistics_proxy_->OnPreDecode(kVideoCodecVP8, -1);

  statistics_proxy_.reset();
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.Decoded.Vp8.Qp"));
}

TEST_F(ReceiveStatisticsProxyTest,
       KeyFrameHistogramNotUpdatedForTooFewSamples) {
  const bool kIsKeyFrame = false;
  const int kFrameSizeBytes = 1000;

  for (int i = 0; i < kMinRequiredSamples - 1; ++i)
    statistics_proxy_->OnCompleteFrame(kIsKeyFrame, kFrameSizeBytes,
                                       VideoContentType::UNSPECIFIED);

  EXPECT_EQ(0, statistics_proxy_->GetStats().frame_counts.key_frames);
  EXPECT_EQ(kMinRequiredSamples - 1,
            statistics_proxy_->GetStats().frame_counts.delta_frames);

  statistics_proxy_.reset();
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.KeyFramesReceivedInPermille"));
}

TEST_F(ReceiveStatisticsProxyTest,
       KeyFrameHistogramUpdatedForMinRequiredSamples) {
  const bool kIsKeyFrame = false;
  const int kFrameSizeBytes = 1000;

  for (int i = 0; i < kMinRequiredSamples; ++i)
    statistics_proxy_->OnCompleteFrame(kIsKeyFrame, kFrameSizeBytes,
                                       VideoContentType::UNSPECIFIED);

  EXPECT_EQ(0, statistics_proxy_->GetStats().frame_counts.key_frames);
  EXPECT_EQ(kMinRequiredSamples,
            statistics_proxy_->GetStats().frame_counts.delta_frames);

  statistics_proxy_.reset();
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.KeyFramesReceivedInPermille"));
  EXPECT_EQ(1,
            metrics::NumEvents("WebRTC.Video.KeyFramesReceivedInPermille", 0));
}

TEST_F(ReceiveStatisticsProxyTest, KeyFrameHistogramIsUpdated) {
  const int kFrameSizeBytes = 1000;

  for (int i = 0; i < kMinRequiredSamples; ++i)
    statistics_proxy_->OnCompleteFrame(true, kFrameSizeBytes,
                                       VideoContentType::UNSPECIFIED);

  for (int i = 0; i < kMinRequiredSamples; ++i)
    statistics_proxy_->OnCompleteFrame(false, kFrameSizeBytes,
                                       VideoContentType::UNSPECIFIED);

  EXPECT_EQ(kMinRequiredSamples,
            statistics_proxy_->GetStats().frame_counts.key_frames);
  EXPECT_EQ(kMinRequiredSamples,
            statistics_proxy_->GetStats().frame_counts.delta_frames);

  statistics_proxy_.reset();
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.KeyFramesReceivedInPermille"));
  EXPECT_EQ(
      1, metrics::NumEvents("WebRTC.Video.KeyFramesReceivedInPermille", 500));
}

TEST_F(ReceiveStatisticsProxyTest, TimingHistogramsNotUpdatedForTooFewSamples) {
  const int kDecodeMs = 1;
  const int kMaxDecodeMs = 2;
  const int kCurrentDelayMs = 3;
  const int kTargetDelayMs = 4;
  const int kJitterBufferMs = 5;
  const int kMinPlayoutDelayMs = 6;
  const int kRenderDelayMs = 7;

  for (int i = 0; i < kMinRequiredSamples - 1; ++i) {
    statistics_proxy_->OnFrameBufferTimingsUpdated(
        kDecodeMs, kMaxDecodeMs, kCurrentDelayMs, kTargetDelayMs,
        kJitterBufferMs, kMinPlayoutDelayMs, kRenderDelayMs);
  }

  statistics_proxy_.reset();
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.DecodeTimeInMs"));
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.JitterBufferDelayInMs"));
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.TargetDelayInMs"));
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.CurrentDelayInMs"));
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.OnewayDelayInMs"));
}

TEST_F(ReceiveStatisticsProxyTest, TimingHistogramsAreUpdated) {
  const int kDecodeMs = 1;
  const int kMaxDecodeMs = 2;
  const int kCurrentDelayMs = 3;
  const int kTargetDelayMs = 4;
  const int kJitterBufferMs = 5;
  const int kMinPlayoutDelayMs = 6;
  const int kRenderDelayMs = 7;

  for (int i = 0; i < kMinRequiredSamples; ++i) {
    statistics_proxy_->OnFrameBufferTimingsUpdated(
        kDecodeMs, kMaxDecodeMs, kCurrentDelayMs, kTargetDelayMs,
        kJitterBufferMs, kMinPlayoutDelayMs, kRenderDelayMs);
  }

  statistics_proxy_.reset();
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.DecodeTimeInMs"));
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.JitterBufferDelayInMs"));
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.TargetDelayInMs"));
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.CurrentDelayInMs"));
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.OnewayDelayInMs"));

  EXPECT_EQ(1, metrics::NumEvents("WebRTC.Video.DecodeTimeInMs", kDecodeMs));
  EXPECT_EQ(1, metrics::NumEvents("WebRTC.Video.JitterBufferDelayInMs",
                                  kJitterBufferMs));
  EXPECT_EQ(1,
            metrics::NumEvents("WebRTC.Video.TargetDelayInMs", kTargetDelayMs));
  EXPECT_EQ(
      1, metrics::NumEvents("WebRTC.Video.CurrentDelayInMs", kCurrentDelayMs));
  EXPECT_EQ(1,
            metrics::NumEvents("WebRTC.Video.OnewayDelayInMs", kTargetDelayMs));
}

TEST_F(ReceiveStatisticsProxyTest, DoesNotReportStaleFramerates) {
  const int kDefaultFps = 30;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i < kDefaultFps; ++i) {
    // Since OnRenderedFrame is never called the fps in each sample will be 0,
    // i.e. bad
    frame.set_ntp_time_ms(fake_clock_.CurrentNtpInMilliseconds());
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                      VideoContentType::UNSPECIFIED);
    statistics_proxy_->OnRenderedFrame(frame);
    fake_clock_.AdvanceTimeMilliseconds(1000 / kDefaultFps);
  }

  EXPECT_EQ(kDefaultFps, statistics_proxy_->GetStats().decode_frame_rate);
  EXPECT_EQ(kDefaultFps, statistics_proxy_->GetStats().render_frame_rate);

  // FPS trackers in stats proxy have a 1000ms sliding window.
  fake_clock_.AdvanceTimeMilliseconds(1000);
  EXPECT_EQ(0, statistics_proxy_->GetStats().decode_frame_rate);
  EXPECT_EQ(0, statistics_proxy_->GetStats().render_frame_rate);
}

TEST_F(ReceiveStatisticsProxyTest, GetStatsReportsReceivedFrameStats) {
  EXPECT_EQ(0, statistics_proxy_->GetStats().width);
  EXPECT_EQ(0, statistics_proxy_->GetStats().height);
  EXPECT_EQ(0u, statistics_proxy_->GetStats().frames_rendered);

  statistics_proxy_->OnRenderedFrame(CreateFrame(kWidth, kHeight));

  EXPECT_EQ(kWidth, statistics_proxy_->GetStats().width);
  EXPECT_EQ(kHeight, statistics_proxy_->GetStats().height);
  EXPECT_EQ(1u, statistics_proxy_->GetStats().frames_rendered);
}

TEST_F(ReceiveStatisticsProxyTest,
       ReceivedFrameHistogramsAreNotUpdatedForTooFewSamples) {
  for (int i = 0; i < kMinRequiredSamples - 1; ++i)
    statistics_proxy_->OnRenderedFrame(CreateFrame(kWidth, kHeight));

  statistics_proxy_.reset();
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.ReceivedWidthInPixels"));
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.ReceivedHeightInPixels"));
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.RenderFramesPerSecond"));
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.RenderSqrtPixelsPerSecond"));
}

TEST_F(ReceiveStatisticsProxyTest, ReceivedFrameHistogramsAreUpdated) {
  for (int i = 0; i < kMinRequiredSamples; ++i)
    statistics_proxy_->OnRenderedFrame(CreateFrame(kWidth, kHeight));

  statistics_proxy_.reset();
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.ReceivedWidthInPixels"));
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.ReceivedHeightInPixels"));
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.RenderFramesPerSecond"));
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.RenderSqrtPixelsPerSecond"));
  EXPECT_EQ(1,
            metrics::NumEvents("WebRTC.Video.ReceivedWidthInPixels", kWidth));
  EXPECT_EQ(1,
            metrics::NumEvents("WebRTC.Video.ReceivedHeightInPixels", kHeight));
}

TEST_F(ReceiveStatisticsProxyTest, ZeroDelayReportedIfFrameNotDelayed) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                    VideoContentType::UNSPECIFIED);

  // Frame not delayed, delayed frames to render: 0%.
  const int64_t kNowMs = fake_clock_.TimeInMilliseconds();
  statistics_proxy_->OnRenderedFrame(CreateFrameWithRenderTimeMs(kNowMs));

  // Min run time has passed.
  fake_clock_.AdvanceTimeMilliseconds((metrics::kMinRunTimeInSeconds * 1000));
  statistics_proxy_.reset();
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.DelayedFramesToRenderer"));
  EXPECT_EQ(1, metrics::NumEvents("WebRTC.Video.DelayedFramesToRenderer", 0));
  EXPECT_EQ(0, metrics::NumSamples(
                   "WebRTC.Video.DelayedFramesToRenderer_AvgDelayInMs"));
}

TEST_F(ReceiveStatisticsProxyTest,
       DelayedFrameHistogramsAreNotUpdatedIfMinRuntimeHasNotPassed) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                    VideoContentType::UNSPECIFIED);

  // Frame not delayed, delayed frames to render: 0%.
  const int64_t kNowMs = fake_clock_.TimeInMilliseconds();
  statistics_proxy_->OnRenderedFrame(CreateFrameWithRenderTimeMs(kNowMs));

  // Min run time has not passed.
  fake_clock_.AdvanceTimeMilliseconds((metrics::kMinRunTimeInSeconds * 1000) -
                                      1);
  statistics_proxy_.reset();
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.DelayedFramesToRenderer"));
  EXPECT_EQ(0, metrics::NumSamples(
                   "WebRTC.Video.DelayedFramesToRenderer_AvgDelayInMs"));
}

TEST_F(ReceiveStatisticsProxyTest,
       DelayedFramesHistogramsAreNotUpdatedIfNoRenderedFrames) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                    VideoContentType::UNSPECIFIED);

  // Min run time has passed. No rendered frames.
  fake_clock_.AdvanceTimeMilliseconds((metrics::kMinRunTimeInSeconds * 1000));
  statistics_proxy_.reset();
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.DelayedFramesToRenderer"));
  EXPECT_EQ(0, metrics::NumSamples(
                   "WebRTC.Video.DelayedFramesToRenderer_AvgDelayInMs"));
}

TEST_F(ReceiveStatisticsProxyTest, DelayReportedIfFrameIsDelayed) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                    VideoContentType::UNSPECIFIED);

  // Frame delayed 1 ms, delayed frames to render: 100%.
  const int64_t kNowMs = fake_clock_.TimeInMilliseconds();
  statistics_proxy_->OnRenderedFrame(CreateFrameWithRenderTimeMs(kNowMs - 1));

  // Min run time has passed.
  fake_clock_.AdvanceTimeMilliseconds((metrics::kMinRunTimeInSeconds * 1000));
  statistics_proxy_.reset();
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.DelayedFramesToRenderer"));
  EXPECT_EQ(1, metrics::NumEvents("WebRTC.Video.DelayedFramesToRenderer", 100));
  EXPECT_EQ(1, metrics::NumSamples(
                   "WebRTC.Video.DelayedFramesToRenderer_AvgDelayInMs"));
  EXPECT_EQ(1, metrics::NumEvents(
                   "WebRTC.Video.DelayedFramesToRenderer_AvgDelayInMs", 1));
}

TEST_F(ReceiveStatisticsProxyTest, AverageDelayOfDelayedFramesIsReported) {
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt,
                                    VideoContentType::UNSPECIFIED);

  // Two frames delayed (6 ms, 10 ms), delayed frames to render: 50%.
  const int64_t kNowMs = fake_clock_.TimeInMilliseconds();
  statistics_proxy_->OnRenderedFrame(CreateFrameWithRenderTimeMs(kNowMs - 10));
  statistics_proxy_->OnRenderedFrame(CreateFrameWithRenderTimeMs(kNowMs - 6));
  statistics_proxy_->OnRenderedFrame(CreateFrameWithRenderTimeMs(kNowMs));
  statistics_proxy_->OnRenderedFrame(CreateFrameWithRenderTimeMs(kNowMs + 1));

  // Min run time has passed.
  fake_clock_.AdvanceTimeMilliseconds((metrics::kMinRunTimeInSeconds * 1000));
  statistics_proxy_.reset();
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.DelayedFramesToRenderer"));
  EXPECT_EQ(1, metrics::NumEvents("WebRTC.Video.DelayedFramesToRenderer", 50));
  EXPECT_EQ(1, metrics::NumSamples(
                   "WebRTC.Video.DelayedFramesToRenderer_AvgDelayInMs"));
  EXPECT_EQ(1, metrics::NumEvents(
                   "WebRTC.Video.DelayedFramesToRenderer_AvgDelayInMs", 8));
}

TEST_F(ReceiveStatisticsProxyTest,
       RtcpHistogramsNotUpdatedIfMinRuntimeHasNotPassed) {
  InsertFirstRtpPacket(kRemoteSsrc);
  fake_clock_.AdvanceTimeMilliseconds((metrics::kMinRunTimeInSeconds * 1000) -
                                      1);

  RtcpPacketTypeCounter counter;
  statistics_proxy_->RtcpPacketTypesCounterUpdated(kRemoteSsrc, counter);

  statistics_proxy_.reset();
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.FirPacketsSentPerMinute"));
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.PliPacketsSentPerMinute"));
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.NackPacketsSentPerMinute"));
}

TEST_F(ReceiveStatisticsProxyTest, RtcpHistogramsAreUpdated) {
  InsertFirstRtpPacket(kRemoteSsrc);
  fake_clock_.AdvanceTimeMilliseconds(metrics::kMinRunTimeInSeconds * 1000);

  const uint32_t kFirPackets = 100;
  const uint32_t kPliPackets = 200;
  const uint32_t kNackPackets = 300;

  RtcpPacketTypeCounter counter;
  counter.fir_packets = kFirPackets;
  counter.pli_packets = kPliPackets;
  counter.nack_packets = kNackPackets;
  statistics_proxy_->RtcpPacketTypesCounterUpdated(kRemoteSsrc, counter);

  statistics_proxy_.reset();
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.FirPacketsSentPerMinute"));
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.PliPacketsSentPerMinute"));
  EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.NackPacketsSentPerMinute"));
  EXPECT_EQ(
      1, metrics::NumEvents("WebRTC.Video.FirPacketsSentPerMinute",
                            kFirPackets * 60 / metrics::kMinRunTimeInSeconds));
  EXPECT_EQ(
      1, metrics::NumEvents("WebRTC.Video.PliPacketsSentPerMinute",
                            kPliPackets * 60 / metrics::kMinRunTimeInSeconds));
  EXPECT_EQ(
      1, metrics::NumEvents("WebRTC.Video.NackPacketsSentPerMinute",
                            kNackPackets * 60 / metrics::kMinRunTimeInSeconds));
}

INSTANTIATE_TEST_CASE_P(ContentTypes,
                        ReceiveStatisticsProxyTest,
                        ::testing::Values(VideoContentType::UNSPECIFIED,
                                          VideoContentType::SCREENSHARE));

TEST_P(ReceiveStatisticsProxyTest, InterFrameDelaysAreReported) {
  const VideoContentType content_type = GetParam();
  const int kInterFrameDelayMs = 33;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i < kMinRequiredSamples; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);
    fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);
  }
  // One extra with double the interval.
  fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);

  statistics_proxy_.reset();
  const int kExpectedInterFrame =
      (kInterFrameDelayMs * (kMinRequiredSamples - 1) +
       kInterFrameDelayMs * 2) /
      kMinRequiredSamples;
  if (videocontenttypehelpers::IsScreenshare(content_type)) {
    EXPECT_EQ(
        kExpectedInterFrame,
        metrics::MinSample("WebRTC.Video.Screenshare.InterframeDelayInMs"));
    EXPECT_EQ(
        kInterFrameDelayMs * 2,
        metrics::MinSample("WebRTC.Video.Screenshare.InterframeDelayMaxInMs"));
  } else {
    EXPECT_EQ(kExpectedInterFrame,
              metrics::MinSample("WebRTC.Video.InterframeDelayInMs"));
    EXPECT_EQ(kInterFrameDelayMs * 2,
              metrics::MinSample("WebRTC.Video.InterframeDelayMaxInMs"));
  }
}

TEST_P(ReceiveStatisticsProxyTest, InterFrameDelaysPercentilesAreReported) {
  const VideoContentType content_type = GetParam();
  const int kInterFrameDelayMs = 33;
  const int kLastFivePercentsSamples = kMinRequiredSamples * 5 / 100;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i <= kMinRequiredSamples - kLastFivePercentsSamples; ++i) {
    fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);
  }
  // Last 5% of intervals are double in size.
  for (int i = 0; i < kLastFivePercentsSamples; ++i) {
    fake_clock_.AdvanceTimeMilliseconds(2 * kInterFrameDelayMs);
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);
  }
  // Final sample is outlier and 10 times as big.
  fake_clock_.AdvanceTimeMilliseconds(10 * kInterFrameDelayMs);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);

  statistics_proxy_.reset();
  const int kExpectedInterFrame = kInterFrameDelayMs * 2;
  if (videocontenttypehelpers::IsScreenshare(content_type)) {
    EXPECT_EQ(kExpectedInterFrame,
              metrics::MinSample(
                  "WebRTC.Video.Screenshare.InterframeDelay95PercentileInMs"));
  } else {
    EXPECT_EQ(
        kExpectedInterFrame,
        metrics::MinSample("WebRTC.Video.InterframeDelay95PercentileInMs"));
  }
}

TEST_P(ReceiveStatisticsProxyTest, MaxInterFrameDelayOnlyWithValidAverage) {
  const VideoContentType content_type = GetParam();
  const int kInterFrameDelayMs = 33;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i < kMinRequiredSamples; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);
    fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);
  }

  // |kMinRequiredSamples| samples, and thereby intervals, is required. That
  // means we're one frame short of having a valid data set.
  statistics_proxy_.reset();
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.InterframeDelayInMs"));
  EXPECT_EQ(0, metrics::NumSamples("WebRTC.Video.InterframeDelayMaxInMs"));
  EXPECT_EQ(
      0, metrics::NumSamples("WebRTC.Video.Screenshare.InterframeDelayInMs"));
  EXPECT_EQ(0, metrics::NumSamples(
                   "WebRTC.Video.Screenshare.InterframeDelayMaxInMs"));
}

TEST_P(ReceiveStatisticsProxyTest, MaxInterFrameDelayOnlyWithPause) {
  const VideoContentType content_type = GetParam();
  const int kInterFrameDelayMs = 33;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i <= kMinRequiredSamples; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);
    fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);
  }

  // At this state, we should have a valid inter-frame delay.
  // Indicate stream paused and make a large jump in time.
  statistics_proxy_->OnStreamInactive();
  fake_clock_.AdvanceTimeMilliseconds(5000);

  // Insert two more frames. The interval during the pause should be disregarded
  // in the stats.
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);
  fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);

  statistics_proxy_.reset();
  if (videocontenttypehelpers::IsScreenshare(content_type)) {
    EXPECT_EQ(
        1, metrics::NumSamples("WebRTC.Video.Screenshare.InterframeDelayInMs"));
    EXPECT_EQ(1, metrics::NumSamples(
                     "WebRTC.Video.Screenshare.InterframeDelayMaxInMs"));
    EXPECT_EQ(
        kInterFrameDelayMs,
        metrics::MinSample("WebRTC.Video.Screenshare.InterframeDelayInMs"));
    EXPECT_EQ(
        kInterFrameDelayMs,
        metrics::MinSample("WebRTC.Video.Screenshare.InterframeDelayMaxInMs"));
  } else {
    EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.InterframeDelayInMs"));
    EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.InterframeDelayMaxInMs"));
    EXPECT_EQ(kInterFrameDelayMs,
              metrics::MinSample("WebRTC.Video.InterframeDelayInMs"));
    EXPECT_EQ(kInterFrameDelayMs,
              metrics::MinSample("WebRTC.Video.InterframeDelayMaxInMs"));
  }
}

TEST_P(ReceiveStatisticsProxyTest, FreezesAreReported) {
  const VideoContentType content_type = GetParam();
  const int kInterFrameDelayMs = 33;
  const int kFreezeDelayMs = 200;
  const int kCallDurationMs =
      kMinRequiredSamples * kInterFrameDelayMs + kFreezeDelayMs;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i < kMinRequiredSamples; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);
    statistics_proxy_->OnRenderedFrame(frame);
    fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);
  }
  // Add extra freeze.
  fake_clock_.AdvanceTimeMilliseconds(kFreezeDelayMs);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);
  statistics_proxy_->OnRenderedFrame(frame);

  statistics_proxy_.reset();
  const int kExpectedTimeBetweenFreezes =
      kInterFrameDelayMs * (kMinRequiredSamples - 1);
  const int kExpectedNumberFreezesPerMinute = 60 * 1000 / kCallDurationMs;
  if (videocontenttypehelpers::IsScreenshare(content_type)) {
    EXPECT_EQ(
        kFreezeDelayMs + kInterFrameDelayMs,
        metrics::MinSample("WebRTC.Video.Screenshare.MeanFreezeDurationMs"));
    EXPECT_EQ(kExpectedTimeBetweenFreezes,
              metrics::MinSample(
                  "WebRTC.Video.Screenshare.MeanTimeBetweenFreezesMs"));
    EXPECT_EQ(
        kExpectedNumberFreezesPerMinute,
        metrics::MinSample("WebRTC.Video.Screenshare.NumberFreezesPerMinute"));
  } else {
    EXPECT_EQ(kFreezeDelayMs + kInterFrameDelayMs,
              metrics::MinSample("WebRTC.Video.MeanFreezeDurationMs"));
    EXPECT_EQ(kExpectedTimeBetweenFreezes,
              metrics::MinSample("WebRTC.Video.MeanTimeBetweenFreezesMs"));
    EXPECT_EQ(kExpectedNumberFreezesPerMinute,
              metrics::MinSample("WebRTC.Video.NumberFreezesPerMinute"));
  }
}

TEST_P(ReceiveStatisticsProxyTest, HarmonicFrameRateIsReported) {
  const VideoContentType content_type = GetParam();
  const int kInterFrameDelayMs = 33;
  const int kFreezeDelayMs = 200;
  const int kCallDurationMs =
      kMinRequiredSamples * kInterFrameDelayMs + kFreezeDelayMs;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i < kMinRequiredSamples; ++i) {
    fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);
    statistics_proxy_->OnRenderedFrame(frame);
  }
  // Add extra freeze.
  fake_clock_.AdvanceTimeMilliseconds(kFreezeDelayMs);
  statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);
  statistics_proxy_->OnRenderedFrame(frame);

  statistics_proxy_.reset();
  double kSumSquaredInterframeDelaysSecs =
      (kMinRequiredSamples - 1) *
      (kInterFrameDelayMs / 1000.0 * kInterFrameDelayMs / 1000.0);
  kSumSquaredInterframeDelaysSecs +=
      kFreezeDelayMs / 1000.0 * kFreezeDelayMs / 1000.0;
  const int kExpectedHarmonicFrameRateFps =
      std::round(kCallDurationMs / (1000 * kSumSquaredInterframeDelaysSecs));
  if (videocontenttypehelpers::IsScreenshare(content_type)) {
    EXPECT_EQ(kExpectedHarmonicFrameRateFps,
              metrics::MinSample("WebRTC.Video.Screenshare.HarmonicFrameRate"));
  } else {
    EXPECT_EQ(kExpectedHarmonicFrameRateFps,
              metrics::MinSample("WebRTC.Video.HarmonicFrameRate"));
  }
}

TEST_P(ReceiveStatisticsProxyTest, PausesAreIgnored) {
  const VideoContentType content_type = GetParam();
  const int kInterFrameDelayMs = 33;
  const int kPauseDurationMs = 10000;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i <= kMinRequiredSamples; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);
    statistics_proxy_->OnRenderedFrame(frame);
    fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);
  }
  // Add a pause.
  fake_clock_.AdvanceTimeMilliseconds(kPauseDurationMs);
  statistics_proxy_->OnStreamInactive();

  // Second playback interval with triple the length.
  for (int i = 0; i <= kMinRequiredSamples * 3; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);
    statistics_proxy_->OnRenderedFrame(frame);
    fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);
  }

  statistics_proxy_.reset();
  // Average of two playback intervals.
  const int kExpectedTimeBetweenFreezes =
      kInterFrameDelayMs * kMinRequiredSamples * 2;
  if (videocontenttypehelpers::IsScreenshare(content_type)) {
    EXPECT_EQ(-1, metrics::MinSample(
                      "WebRTC.Video.Screenshare.MeanFreezeDurationMs"));
    EXPECT_EQ(kExpectedTimeBetweenFreezes,
              metrics::MinSample(
                  "WebRTC.Video.Screenshare.MeanTimeBetweenFreezesMs"));
  } else {
    EXPECT_EQ(-1, metrics::MinSample("WebRTC.Video.MeanFreezeDurationMs"));
    EXPECT_EQ(kExpectedTimeBetweenFreezes,
              metrics::MinSample("WebRTC.Video.MeanTimeBetweenFreezesMs"));
  }
}

TEST_P(ReceiveStatisticsProxyTest, ManyPausesAtTheBeginning) {
  const VideoContentType content_type = GetParam();
  const int kInterFrameDelayMs = 33;
  const int kPauseDurationMs = 10000;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  for (int i = 0; i <= kMinRequiredSamples; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);
    fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);

    statistics_proxy_->OnStreamInactive();
    fake_clock_.AdvanceTimeMilliseconds(kPauseDurationMs);

    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);
    fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);
  }

  statistics_proxy_.reset();
  // No freezes should be detected, as all long inter-frame delays were pauses.
  if (videocontenttypehelpers::IsScreenshare(content_type)) {
    EXPECT_EQ(-1, metrics::MinSample(
                      "WebRTC.Video.Screenshare.MeanFreezeDurationMs"));
  } else {
    EXPECT_EQ(-1, metrics::MinSample("WebRTC.Video.MeanFreezeDurationMs"));
  }
}

TEST_P(ReceiveStatisticsProxyTest, TimeInHdReported) {
  const VideoContentType content_type = GetParam();
  const int kInterFrameDelayMs = 20;
  webrtc::VideoFrame frame_hd = CreateFrame(1280, 720);
  webrtc::VideoFrame frame_sd = CreateFrame(640, 360);

  // HD frames.
  for (int i = 0; i < kMinRequiredSamples; ++i) {
    statistics_proxy_->OnDecodedFrame(frame_hd, absl::nullopt, content_type);
    statistics_proxy_->OnRenderedFrame(frame_hd);
    fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);
  }
  // SD frames.
  for (int i = 0; i < 2 * kMinRequiredSamples; ++i) {
    statistics_proxy_->OnDecodedFrame(frame_sd, absl::nullopt, content_type);
    statistics_proxy_->OnRenderedFrame(frame_sd);
    fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);
  }
  // Extra last frame.
  statistics_proxy_->OnRenderedFrame(frame_sd);

  statistics_proxy_.reset();
  const int kExpectedTimeInHdPercents = 33;
  if (videocontenttypehelpers::IsScreenshare(content_type)) {
    EXPECT_EQ(
        kExpectedTimeInHdPercents,
        metrics::MinSample("WebRTC.Video.Screenshare.TimeInHdPercentage"));
  } else {
    EXPECT_EQ(kExpectedTimeInHdPercents,
              metrics::MinSample("WebRTC.Video.TimeInHdPercentage"));
  }
}

TEST_P(ReceiveStatisticsProxyTest, TimeInBlockyVideoReported) {
  const VideoContentType content_type = GetParam();
  const int kInterFrameDelayMs = 20;
  const int kHighQp = 80;
  const int kLowQp = 30;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  // High quality frames.
  for (int i = 0; i < kMinRequiredSamples; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, kLowQp, content_type);
    statistics_proxy_->OnRenderedFrame(frame);
    fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);
  }
  // Blocky frames.
  for (int i = 0; i < 2 * kMinRequiredSamples; ++i) {
    statistics_proxy_->OnDecodedFrame(frame, kHighQp, content_type);
    statistics_proxy_->OnRenderedFrame(frame);
    fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);
  }
  // Extra last frame.
  statistics_proxy_->OnDecodedFrame(frame, kHighQp, content_type);
  statistics_proxy_->OnRenderedFrame(frame);

  statistics_proxy_.reset();
  const int kExpectedTimeInHdPercents = 66;
  if (videocontenttypehelpers::IsScreenshare(content_type)) {
    EXPECT_EQ(kExpectedTimeInHdPercents,
              metrics::MinSample(
                  "WebRTC.Video.Screenshare.TimeInBlockyVideoPercentage"));
  } else {
    EXPECT_EQ(kExpectedTimeInHdPercents,
              metrics::MinSample("WebRTC.Video.TimeInBlockyVideoPercentage"));
  }
}

TEST_P(ReceiveStatisticsProxyTest, DownscalesReported) {
  const VideoContentType content_type = GetParam();
  const int kInterFrameDelayMs = 2000;  // To ensure long enough call duration.

  webrtc::VideoFrame frame_hd = CreateFrame(1280, 720);
  webrtc::VideoFrame frame_sd = CreateFrame(640, 360);
  webrtc::VideoFrame frame_ld = CreateFrame(320, 180);

  // Call once to pass content type.
  statistics_proxy_->OnDecodedFrame(frame_hd, absl::nullopt, content_type);

  statistics_proxy_->OnRenderedFrame(frame_hd);
  fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);

  // Downscale.
  statistics_proxy_->OnRenderedFrame(frame_sd);
  fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);

  // Downscale.
  statistics_proxy_->OnRenderedFrame(frame_ld);
  fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs);

  statistics_proxy_.reset();
  const int kExpectedDownscales = 30;  // 2 per 4 seconds = 30 per minute.
  if (videocontenttypehelpers::IsScreenshare(content_type)) {
    EXPECT_EQ(
        kExpectedDownscales,
        metrics::MinSample(
            "WebRTC.Video.Screenshare.NumberResolutionDownswitchesPerMinute"));
  } else {
    EXPECT_EQ(kExpectedDownscales,
              metrics::MinSample(
                  "WebRTC.Video.NumberResolutionDownswitchesPerMinute"));
  }
}

TEST_P(ReceiveStatisticsProxyTest, StatsAreSlicedOnSimulcastAndExperiment) {
  VideoContentType content_type = GetParam();
  const uint8_t experiment_id = 1;
  videocontenttypehelpers::SetExperimentId(&content_type, experiment_id);
  const int kInterFrameDelayMs1 = 30;
  const int kInterFrameDelayMs2 = 50;
  webrtc::VideoFrame frame = CreateFrame(kWidth, kHeight);

  videocontenttypehelpers::SetSimulcastId(&content_type, 1);
  for (int i = 0; i <= kMinRequiredSamples; ++i) {
    fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs1);
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);
  }

  videocontenttypehelpers::SetSimulcastId(&content_type, 2);
  for (int i = 0; i <= kMinRequiredSamples; ++i) {
    fake_clock_.AdvanceTimeMilliseconds(kInterFrameDelayMs2);
    statistics_proxy_->OnDecodedFrame(frame, absl::nullopt, content_type);
  }
  statistics_proxy_.reset();

  if (videocontenttypehelpers::IsScreenshare(content_type)) {
    EXPECT_EQ(
        1, metrics::NumSamples("WebRTC.Video.Screenshare.InterframeDelayInMs"));
    EXPECT_EQ(1, metrics::NumSamples(
                     "WebRTC.Video.Screenshare.InterframeDelayMaxInMs"));
    EXPECT_EQ(1, metrics::NumSamples(
                     "WebRTC.Video.Screenshare.InterframeDelayInMs.S0"));
    EXPECT_EQ(1, metrics::NumSamples(
                     "WebRTC.Video.Screenshare.InterframeDelayMaxInMs.S0"));
    EXPECT_EQ(1, metrics::NumSamples(
                     "WebRTC.Video.Screenshare.InterframeDelayInMs.S1"));
    EXPECT_EQ(1, metrics::NumSamples(
                     "WebRTC.Video.Screenshare.InterframeDelayMaxInMs.S1"));
    EXPECT_EQ(1,
              metrics::NumSamples("WebRTC.Video.Screenshare.InterframeDelayInMs"
                                  ".ExperimentGroup0"));
    EXPECT_EQ(
        1, metrics::NumSamples("WebRTC.Video.Screenshare.InterframeDelayMaxInMs"
                               ".ExperimentGroup0"));
    EXPECT_EQ(
        kInterFrameDelayMs1,
        metrics::MinSample("WebRTC.Video.Screenshare.InterframeDelayInMs.S0"));
    EXPECT_EQ(
        kInterFrameDelayMs2,
        metrics::MinSample("WebRTC.Video.Screenshare.InterframeDelayInMs.S1"));
    EXPECT_EQ(
        (kInterFrameDelayMs1 + kInterFrameDelayMs2) / 2,
        metrics::MinSample("WebRTC.Video.Screenshare.InterframeDelayInMs"));
    EXPECT_EQ(
        kInterFrameDelayMs2,
        metrics::MinSample("WebRTC.Video.Screenshare.InterframeDelayMaxInMs"));
    EXPECT_EQ(
        (kInterFrameDelayMs1 + kInterFrameDelayMs2) / 2,
        metrics::MinSample(
            "WebRTC.Video.Screenshare.InterframeDelayInMs.ExperimentGroup0"));
  } else {
    EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.InterframeDelayInMs"));
    EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.InterframeDelayMaxInMs"));
    EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.InterframeDelayInMs.S0"));
    EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.InterframeDelayMaxInMs.S0"));
    EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.InterframeDelayInMs.S1"));
    EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.InterframeDelayMaxInMs.S1"));
    EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.InterframeDelayInMs"
                                     ".ExperimentGroup0"));
    EXPECT_EQ(1, metrics::NumSamples("WebRTC.Video.InterframeDelayMaxInMs"
                                     ".ExperimentGroup0"));
    EXPECT_EQ(kInterFrameDelayMs1,
              metrics::MinSample("WebRTC.Video.InterframeDelayInMs.S0"));
    EXPECT_EQ(kInterFrameDelayMs2,
              metrics::MinSample("WebRTC.Video.InterframeDelayInMs.S1"));
    EXPECT_EQ((kInterFrameDelayMs1 + kInterFrameDelayMs2) / 2,
              metrics::MinSample("WebRTC.Video.InterframeDelayInMs"));
    EXPECT_EQ(kInterFrameDelayMs2,
              metrics::MinSample("WebRTC.Video.InterframeDelayMaxInMs"));
    EXPECT_EQ((kInterFrameDelayMs1 + kInterFrameDelayMs2) / 2,
              metrics::MinSample(
                  "WebRTC.Video.InterframeDelayInMs.ExperimentGroup0"));
  }
}
}  // namespace webrtc