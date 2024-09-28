/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "moxygen/MoQFramer.h"

namespace moxygen {

folly::Expected<std::string, ErrorCode> parseFixedString(
    folly::io::Cursor& cursor) {
  auto strLength = quic::decodeQuicInteger(cursor);
  if (!strLength || !cursor.canAdvance(strLength->first)) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  auto res = cursor.readFixedString(strLength->first);
  return res;
}

folly::Expected<folly::Unit, ErrorCode> parseSetupParams(
    folly::io::Cursor& cursor,
    size_t numParams,
    std::vector<SetupParameter>& params) {
  for (auto i = 0u; i < numParams; i++) {
    auto key = quic::decodeQuicInteger(cursor);
    if (!key) {
      return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
    }
    SetupParameter p;
    p.key = key->first;
    if (p.key == folly::to_underlying(SetupKey::ROLE)) {
      auto res = quic::decodeQuicInteger(cursor);
      if (!res) {
        return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
      }
      res = quic::decodeQuicInteger(cursor, res->first);
      if (!res) {
        return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
      }
      p.asUint64 = res->first;
    } else {
      auto res = parseFixedString(cursor);
      if (!res) {
        return folly::makeUnexpected(res.error());
      }
      p.asString = std::move(res.value());
    }
    params.emplace_back(std::move(p));
  }
  return folly::unit;
}

folly::Expected<FullTrackName, ErrorCode> parseFullTrackName(
    folly::io::Cursor& cursor) {
  FullTrackName fullTrackName;
  auto res = parseFixedString(cursor);
  if (!res) {
    return folly::makeUnexpected(res.error());
  }
  fullTrackName.trackNamespace = std::move(res.value());
  res = parseFixedString(cursor);
  if (!res) {
    return folly::makeUnexpected(res.error());
  }
  fullTrackName.trackName = std::move(res.value());
  return fullTrackName;
}

folly::Expected<AbsoluteLocation, ErrorCode> parseAbsoluteLocation(
    folly::io::Cursor& cursor) {
  AbsoluteLocation location;
  auto group = quic::decodeQuicInteger(cursor);
  if (!group) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  location.group = group->first;

  auto object = quic::decodeQuicInteger(cursor);
  if (!object) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  location.object = object->first;

  return location;
}

folly::Expected<ClientSetup, ErrorCode> parseClientSetup(
    folly::io::Cursor& cursor) noexcept {
  ClientSetup clientSetup;
  auto numVersions = quic::decodeQuicInteger(cursor);
  if (!numVersions) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  for (auto i = 0; i < numVersions->first; i++) {
    auto version = quic::decodeQuicInteger(cursor);
    if (!version) {
      return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
    }
    clientSetup.supportedVersions.push_back(version->first);
  }
  auto numParams = quic::decodeQuicInteger(cursor);
  if (!numParams) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  auto res = parseSetupParams(cursor, numParams->first, clientSetup.params);
  if (res.hasError()) {
    return folly::makeUnexpected(res.error());
  }
  return clientSetup;
}

folly::Expected<ServerSetup, ErrorCode> parseServerSetup(
    folly::io::Cursor& cursor) noexcept {
  ServerSetup serverSetup;
  auto version = quic::decodeQuicInteger(cursor);
  if (!version) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  serverSetup.selectedVersion = version->first;
  auto numParams = quic::decodeQuicInteger(cursor);
  if (!numParams) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  auto res = parseSetupParams(cursor, numParams->first, serverSetup.params);
  if (res.hasError()) {
    return folly::makeUnexpected(res.error());
  }
  return serverSetup;
}

folly::Expected<ObjectHeader, ErrorCode> parseObjectHeader(
    folly::io::Cursor& cursor,
    FrameType frameType) noexcept {
  DCHECK(
      frameType == FrameType::OBJECT_STREAM ||
      frameType == FrameType::OBJECT_DATAGRAM);
  ObjectHeader objectHeader;
  auto subscribeID = quic::decodeQuicInteger(cursor);
  if (!subscribeID) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  objectHeader.subscribeID = subscribeID->first;
  auto trackAlias = quic::decodeQuicInteger(cursor);
  if (!trackAlias) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  objectHeader.trackAlias = trackAlias->first;
  auto group = quic::decodeQuicInteger(cursor);
  if (!group) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  objectHeader.group = group->first;
  auto id = quic::decodeQuicInteger(cursor);
  if (!id) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  objectHeader.id = id->first;
  if (!cursor.canAdvance(1)) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  objectHeader.priority = cursor.readBE<uint8_t>();
  auto objectStatus = quic::decodeQuicInteger(cursor);
  if (!objectStatus) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  if (objectStatus->first >
      folly::to_underlying(ObjectStatus::END_OF_TRACK_AND_GROUP)) {
    return folly::makeUnexpected(ErrorCode::PARSE_ERROR);
  }
  objectHeader.status = ObjectStatus(objectStatus->first);
  if (frameType == FrameType::OBJECT_STREAM) {
    objectHeader.forwardPreference = ForwardPreference::Object;
    // length is not present and runs to the end of the stream
  } else {
    // This is for datagrams
    objectHeader.forwardPreference = ForwardPreference::Datagram;
    // length is not present and runs to the end of the stream
  }
  return objectHeader;
}

folly::Expected<ObjectHeader, ErrorCode> parseStreamHeader(
    folly::io::Cursor& cursor,
    FrameType frameType) noexcept {
  DCHECK(
      frameType == FrameType::STREAM_HEADER_TRACK ||
      frameType == FrameType::STREAM_HEADER_GROUP);
  ObjectHeader objectHeader;
  auto subscribeID = quic::decodeQuicInteger(cursor);
  if (!subscribeID) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  objectHeader.subscribeID = subscribeID->first;
  auto trackAlias = quic::decodeQuicInteger(cursor);
  if (!trackAlias) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  objectHeader.trackAlias = trackAlias->first;
  if (frameType == FrameType::STREAM_HEADER_GROUP) {
    auto group = quic::decodeQuicInteger(cursor);
    if (!group) {
      return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
    }
    objectHeader.group = group->first;
    objectHeader.forwardPreference = ForwardPreference::Group;
  } else {
    objectHeader.forwardPreference = ForwardPreference::Track;
  }
  auto priority = quic::decodeQuicInteger(cursor);
  if (!priority) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  objectHeader.priority = priority->first;
  return objectHeader;
}

folly::Expected<ObjectHeader, ErrorCode> parseMultiObjectHeader(
    folly::io::Cursor& cursor,
    FrameType frameType,
    const ObjectHeader& headerTemplate) noexcept {
  DCHECK(
      frameType == FrameType::STREAM_HEADER_TRACK ||
      frameType == FrameType::STREAM_HEADER_GROUP);
  ObjectHeader objectHeader = headerTemplate;
  if (frameType == FrameType::STREAM_HEADER_TRACK) {
    auto group = quic::decodeQuicInteger(cursor);
    if (!group) {
      return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
    }
    objectHeader.group = group->first;
    objectHeader.forwardPreference = ForwardPreference::Track;
  } else {
    objectHeader.forwardPreference = ForwardPreference::Group;
  }
  auto id = quic::decodeQuicInteger(cursor);
  if (!id) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  objectHeader.id = id->first;
  auto payloadLength = quic::decodeQuicInteger(cursor);
  if (!payloadLength) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  objectHeader.length = payloadLength->first;

  if (objectHeader.length == 0) {
    auto objectStatus = quic::decodeQuicInteger(cursor);
    if (!objectStatus) {
      return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
    }
    if (objectStatus->first >
        folly::to_underlying(ObjectStatus::END_OF_TRACK_AND_GROUP)) {
      return folly::makeUnexpected(ErrorCode::PARSE_ERROR);
    }
    objectHeader.status = ObjectStatus(objectStatus->first);
  }

  return objectHeader;
}

folly::Expected<folly::Unit, ErrorCode> parseTrackRequestParams(
    folly::io::Cursor& cursor,
    size_t numParams,
    std::vector<TrackRequestParameter>& params) {
  for (auto i = 0u; i < numParams; i++) {
    auto key = quic::decodeQuicInteger(cursor);
    if (!key) {
      return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
    }
    auto res = parseFixedString(cursor);
    if (!res) {
      return folly::makeUnexpected(res.error());
    }
    params.emplace_back(
        TrackRequestParameter({key->first, std::move(res.value())}));
  }
  return folly::unit;
}

folly::Expected<SubscribeRequest, ErrorCode> parseSubscribeRequest(
    folly::io::Cursor& cursor) noexcept {
  SubscribeRequest subscribeRequest;
  auto subscribeID = quic::decodeQuicInteger(cursor);
  if (!subscribeID) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  subscribeRequest.subscribeID = subscribeID->first;
  auto trackAlias = quic::decodeQuicInteger(cursor);
  if (!trackAlias) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  subscribeRequest.trackAlias = trackAlias->first;
  auto res = parseFullTrackName(cursor);
  if (!res) {
    return folly::makeUnexpected(res.error());
  }
  subscribeRequest.fullTrackName = std::move(res.value());
  if (!cursor.canAdvance(2)) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  subscribeRequest.priority = cursor.readBE<uint8_t>();
  auto order = cursor.readBE<uint8_t>();
  if (order > folly::to_underlying(GroupOrder::NewestFirst)) {
    return folly::makeUnexpected(ErrorCode::INVALID_MESSAGE);
  }
  subscribeRequest.groupOrder = static_cast<GroupOrder>(order);
  auto locType = quic::decodeQuicInteger(cursor);
  if (!locType) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  if (locType->first > folly::to_underlying(LocationType::AbsoluteRange)) {
    return folly::makeUnexpected(ErrorCode::PARSE_ERROR);
  }
  subscribeRequest.locType = LocationType(locType->first);
  if (subscribeRequest.locType == LocationType::AbsoluteStart ||
      subscribeRequest.locType == LocationType::AbsoluteRange) {
    auto location = parseAbsoluteLocation(cursor);
    if (!location) {
      return folly::makeUnexpected(location.error());
    }
    subscribeRequest.start = *location;
  }
  if (subscribeRequest.locType == LocationType::AbsoluteRange) {
    auto location = parseAbsoluteLocation(cursor);
    if (!location) {
      return folly::makeUnexpected(location.error());
    }
    subscribeRequest.end = *location;
  }
  auto numParams = quic::decodeQuicInteger(cursor);
  if (!numParams) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  auto res2 = parseTrackRequestParams(
      cursor, numParams->first, subscribeRequest.params);
  if (!res2) {
    return folly::makeUnexpected(res2.error());
  }
  return subscribeRequest;
}

folly::Expected<SubscribeUpdate, ErrorCode> parseSubscribeUpdate(
    folly::io::Cursor& cursor) noexcept {
  SubscribeUpdate subscribeUpdate;
  auto subscribeID = quic::decodeQuicInteger(cursor);
  if (!subscribeID) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  subscribeUpdate.subscribeID = subscribeID->first;
  auto start = parseAbsoluteLocation(cursor);
  if (!start) {
    return folly::makeUnexpected(start.error());
  }
  subscribeUpdate.start = start.value();
  auto end = parseAbsoluteLocation(cursor);
  if (!end) {
    return folly::makeUnexpected(end.error());
  }
  subscribeUpdate.end = end.value();
  if (!cursor.canAdvance(1)) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  subscribeUpdate.priority = cursor.readBE<uint8_t>();
  auto numParams = quic::decodeQuicInteger(cursor);
  if (!numParams) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  auto res2 =
      parseTrackRequestParams(cursor, numParams->first, subscribeUpdate.params);
  if (!res2) {
    return folly::makeUnexpected(res2.error());
  }
  return subscribeUpdate;
}

folly::Expected<SubscribeOk, ErrorCode> parseSubscribeOk(
    folly::io::Cursor& cursor) noexcept {
  SubscribeOk subscribeOk;
  auto subscribeID = quic::decodeQuicInteger(cursor);
  if (!subscribeID) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  subscribeOk.subscribeID = subscribeID->first;

  auto expires = quic::decodeQuicInteger(cursor);
  if (!expires) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  subscribeOk.expires = std::chrono::milliseconds(expires->first);
  if (!cursor.canAdvance(2)) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  auto order = cursor.readBE<uint8_t>();
  if (order == 0 || order > folly::to_underlying(GroupOrder::NewestFirst)) {
    return folly::makeUnexpected(ErrorCode::INVALID_MESSAGE);
  }
  subscribeOk.groupOrder = static_cast<GroupOrder>(order);
  auto contentExists = cursor.readBE<uint8_t>();
  if (contentExists) {
    auto res = parseAbsoluteLocation(cursor);
    if (!res) {
      return folly::makeUnexpected(res.error());
    }
    subscribeOk.latest = *res;
  }
  auto numParams = quic::decodeQuicInteger(cursor);
  if (!numParams) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  auto res2 =
      parseTrackRequestParams(cursor, numParams->first, subscribeOk.params);
  if (!res2) {
    return folly::makeUnexpected(res2.error());
  }

  return subscribeOk;
}

folly::Expected<SubscribeError, ErrorCode> parseSubscribeError(
    folly::io::Cursor& cursor) noexcept {
  SubscribeError subscribeError;
  auto subscribeID = quic::decodeQuicInteger(cursor);
  if (!subscribeID) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  subscribeError.subscribeID = subscribeID->first;

  auto errorCode = quic::decodeQuicInteger(cursor);
  if (!errorCode) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  subscribeError.errorCode = errorCode->first;

  auto reas = parseFixedString(cursor);
  if (!reas) {
    return folly::makeUnexpected(reas.error());
  }
  subscribeError.reasonPhrase = std::move(reas.value());

  auto retryAlias = quic::decodeQuicInteger(cursor);
  if (!retryAlias) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  if (subscribeError.errorCode ==
      folly::to_underlying(SubscribeErrorCode::RETRY_TRACK_ALIAS)) {
    subscribeError.retryAlias = retryAlias->first;
  }

  return subscribeError;
}

folly::Expected<Unsubscribe, ErrorCode> parseUnsubscribe(
    folly::io::Cursor& cursor) noexcept {
  Unsubscribe unsubscribe;
  auto subscribeID = quic::decodeQuicInteger(cursor);
  if (!subscribeID) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  unsubscribe.subscribeID = subscribeID->first;

  return unsubscribe;
}

folly::Expected<SubscribeDone, ErrorCode> parseSubscribeDone(
    folly::io::Cursor& cursor) noexcept {
  SubscribeDone subscribeDone;
  auto subscribeID = quic::decodeQuicInteger(cursor);
  if (!subscribeID) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  subscribeDone.subscribeID = subscribeID->first;

  auto statusCode = quic::decodeQuicInteger(cursor);
  if (!statusCode) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  subscribeDone.statusCode = SubscribeDoneStatusCode(statusCode->first);

  auto reas = parseFixedString(cursor);
  if (!reas) {
    return folly::makeUnexpected(reas.error());
  }
  subscribeDone.reasonPhrase = std::move(reas.value());

  if (!cursor.canAdvance(1)) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  auto contentExists = cursor.readBE<uint8_t>();
  if (contentExists) {
    auto res = parseAbsoluteLocation(cursor);
    if (!res) {
      return folly::makeUnexpected(res.error());
    }
    subscribeDone.finalObject = *res;
  }

  return subscribeDone;
}

folly::Expected<Announce, ErrorCode> parseAnnounce(
    folly::io::Cursor& cursor) noexcept {
  Announce announce;
  auto res = parseFixedString(cursor);
  if (!res) {
    return folly::makeUnexpected(res.error());
  }
  announce.trackNamespace = std::move(res.value());
  auto numParams = quic::decodeQuicInteger(cursor);
  if (!numParams) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  auto res2 =
      parseTrackRequestParams(cursor, numParams->first, announce.params);
  if (!res2) {
    return folly::makeUnexpected(res2.error());
  }
  return announce;
}

folly::Expected<AnnounceOk, ErrorCode> parseAnnounceOk(
    folly::io::Cursor& cursor) noexcept {
  AnnounceOk announceOk;
  auto res = parseFixedString(cursor);
  if (!res) {
    return folly::makeUnexpected(res.error());
  }
  announceOk.trackNamespace = std::move(res.value());
  return announceOk;
}

folly::Expected<AnnounceError, ErrorCode> parseAnnounceError(
    folly::io::Cursor& cursor) noexcept {
  AnnounceError announceError;
  auto res = parseFixedString(cursor);
  if (!res) {
    return folly::makeUnexpected(res.error());
  }
  announceError.trackNamespace = std::move(res.value());

  auto errorCode = quic::decodeQuicInteger(cursor);
  if (!errorCode) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  announceError.errorCode = errorCode->first;

  res = parseFixedString(cursor);
  if (!res) {
    return folly::makeUnexpected(res.error());
  }
  announceError.reasonPhrase = std::move(res.value());

  return announceError;
}

folly::Expected<Unannounce, ErrorCode> parseUnannounce(
    folly::io::Cursor& cursor) noexcept {
  Unannounce unannounce;
  auto res = parseFixedString(cursor);
  if (!res) {
    return folly::makeUnexpected(res.error());
  }
  unannounce.trackNamespace = std::move(res.value());
  return unannounce;
}

folly::Expected<AnnounceCancel, ErrorCode> parseAnnounceCancel(
    folly::io::Cursor& cursor) noexcept {
  AnnounceCancel announceCancel;
  auto res = parseFixedString(cursor);
  if (!res) {
    return folly::makeUnexpected(res.error());
  }
  announceCancel.trackNamespace = std::move(res.value());

  auto errorCode = quic::decodeQuicInteger(cursor);
  if (!errorCode) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  announceCancel.errorCode = errorCode->first;

  res = parseFixedString(cursor);
  if (!res) {
    return folly::makeUnexpected(res.error());
  }
  announceCancel.reasonPhrase = std::move(res.value());
  return announceCancel;
}

folly::Expected<TrackStatusRequest, ErrorCode> parseTrackStatusRequest(
    folly::io::Cursor& cursor) noexcept {
  TrackStatusRequest trackStatusRequest;
  auto res = parseFullTrackName(cursor);
  if (!res) {
    return folly::makeUnexpected(res.error());
  }
  trackStatusRequest.fullTrackName = std::move(res.value());
  return trackStatusRequest;
}

folly::Expected<TrackStatus, ErrorCode> parseTrackStatus(
    folly::io::Cursor& cursor) noexcept {
  TrackStatus trackStatus;
  auto res = parseFullTrackName(cursor);
  if (!res) {
    return folly::makeUnexpected(res.error());
  }
  auto statusCode = quic::decodeQuicInteger(cursor);
  if (!statusCode) {
    return folly::makeUnexpected(ErrorCode::PARSE_UNDERFLOW);
  }
  if (statusCode->first > folly::to_underlying(TrackStatusCode::UNKNOWN)) {
    return folly::makeUnexpected(ErrorCode::INVALID_MESSAGE);
  }
  trackStatus.statusCode = TrackStatusCode(statusCode->first);
  auto location = parseAbsoluteLocation(cursor);
  if (!location) {
    return folly::makeUnexpected(location.error());
  }
  trackStatus.latestGroupAndObject = *location;
  return trackStatus;
}

folly::Expected<Goaway, ErrorCode> parseGoaway(
    folly::io::Cursor& cursor) noexcept {
  Goaway goaway;
  auto res = parseFixedString(cursor);
  if (!res) {
    return folly::makeUnexpected(res.error());
  }
  goaway.newSessionUri = std::move(res.value());
  return goaway;
}

//// Egress ////

void writeVarint(
    folly::IOBufQueue& buf,
    uint64_t value,
    size_t& size,
    bool& error) noexcept {
  if (error) {
    return;
  }
  folly::io::QueueAppender appender(&buf, kMaxFrameHeaderSize);
  auto appenderOp = [appender = std::move(appender)](auto val) mutable {
    appender.writeBE(val);
  };
  auto res = quic::encodeQuicInteger(value, appenderOp);
  if (res.hasError()) {
    error = true;
  } else {
    size += *res;
  }
}

void writeFixedString(
    folly::IOBufQueue& writeBuf,
    const std::string& str,
    size_t& size,
    bool error) {
  writeVarint(writeBuf, str.size(), size, error);
  if (!error) {
    writeBuf.append(str);
    size += str.size();
  }
}

void writeFullTrackName(
    folly::IOBufQueue& writeBuf,
    const FullTrackName& fullTrackName,
    size_t& size,
    bool error) {
  writeFixedString(writeBuf, fullTrackName.trackNamespace, size, error);
  writeFixedString(writeBuf, fullTrackName.trackName, size, error);
}

WriteResult writeClientSetup(
    folly::IOBufQueue& writeBuf,
    const ClientSetup& clientSetup) noexcept {
  size_t size = 0;
  bool error = false;
  writeVarint(
      writeBuf, folly::to_underlying(FrameType::CLIENT_SETUP), size, error);

  writeVarint(writeBuf, clientSetup.supportedVersions.size(), size, error);
  for (auto version : clientSetup.supportedVersions) {
    writeVarint(writeBuf, version, size, error);
  }

  writeVarint(writeBuf, clientSetup.params.size(), size, error);
  for (auto& param : clientSetup.params) {
    writeVarint(writeBuf, param.key, size, error);
    if (param.key == folly::to_underlying(SetupKey::ROLE)) {
      CHECK_LE(param.asUint64, folly::to_underlying(Role::PUB_AND_SUB));
      writeVarint(writeBuf, 1, size, error);
      writeVarint(writeBuf, param.asUint64, size, error);
    } else {
      writeFixedString(writeBuf, param.asString, size, error);
    }
  }
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  return size;
}

WriteResult writeServerSetup(
    folly::IOBufQueue& writeBuf,
    const ServerSetup& serverSetup) noexcept {
  size_t size = 0;
  bool error = false;
  writeVarint(
      writeBuf, folly::to_underlying(FrameType::SERVER_SETUP), size, error);
  writeVarint(writeBuf, serverSetup.selectedVersion, size, error);
  writeVarint(writeBuf, serverSetup.params.size(), size, error);
  for (auto& param : serverSetup.params) {
    writeVarint(writeBuf, param.key, size, error);
    if (param.key == folly::to_underlying(SetupKey::ROLE)) {
      CHECK_LE(param.asUint64, folly::to_underlying(Role::PUB_AND_SUB));
      writeVarint(writeBuf, 1, size, error);
      writeVarint(writeBuf, param.asUint64, size, error);
    } else {
      writeFixedString(writeBuf, param.asString, size, error);
    }
  }
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  return size;
}

WriteResult writeStreamHeader(
    folly::IOBufQueue& writeBuf,
    const ObjectHeader& objectHeader) noexcept {
  size_t size = 0;
  bool error = false;

  if (objectHeader.forwardPreference == ForwardPreference::Track) {
    writeVarint(
        writeBuf,
        folly::to_underlying(FrameType::STREAM_HEADER_TRACK),
        size,
        error);
  } else if (objectHeader.forwardPreference == ForwardPreference::Group) {
    writeVarint(
        writeBuf,
        folly::to_underlying(FrameType::STREAM_HEADER_GROUP),
        size,
        error);
  } else {
    LOG(FATAL) << "Unsupported forward preference to stream header";
  }
  writeVarint(writeBuf, objectHeader.subscribeID, size, error);
  writeVarint(writeBuf, objectHeader.trackAlias, size, error);
  if (objectHeader.forwardPreference == ForwardPreference::Group) {
    writeVarint(writeBuf, objectHeader.group, size, error);
  }
  writeVarint(writeBuf, objectHeader.priority, size, error);
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  return size;
}

WriteResult writeObject(
    folly::IOBufQueue& writeBuf,
    const ObjectHeader& objectHeader,
    std::unique_ptr<folly::IOBuf> objectPayload) noexcept {
  size_t size = 0;
  bool error = false;
  bool multiObject = true;
  if (objectHeader.forwardPreference == ForwardPreference::Object) {
    writeVarint(
        writeBuf, folly::to_underlying(FrameType::OBJECT_STREAM), size, error);
    multiObject = false;

  } else if (objectHeader.forwardPreference == ForwardPreference::Datagram) {
    writeVarint(
        writeBuf,
        folly::to_underlying(FrameType::OBJECT_DATAGRAM),
        size,
        error);
    multiObject = false;
  }
  if (!multiObject) {
    writeVarint(writeBuf, objectHeader.subscribeID, size, error);
    writeVarint(writeBuf, objectHeader.trackAlias, size, error);
  }
  if (objectHeader.forwardPreference != ForwardPreference::Group) {
    writeVarint(writeBuf, objectHeader.group, size, error);
  }
  writeVarint(writeBuf, objectHeader.id, size, error);
  if (!multiObject) {
    writeBuf.append(&objectHeader.priority, 1);
    writeVarint(
        writeBuf, folly::to_underlying(objectHeader.status), size, error);
  } else {
    CHECK(objectHeader.length) << "Multi-object streams require known length";
    writeVarint(writeBuf, *objectHeader.length, size, error);
    if (*objectHeader.length == 0) {
      writeVarint(
          writeBuf, folly::to_underlying(objectHeader.status), size, error);
    }
  }
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  if (objectPayload) {
    writeBuf.append(std::move(objectPayload));
  }
  return size;
}

WriteResult writeSubscribeRequest(
    folly::IOBufQueue& writeBuf,
    const SubscribeRequest& subscribeRequest) noexcept {
  size_t size = 0;
  bool error = false;
  writeVarint(
      writeBuf, folly::to_underlying(FrameType::SUBSCRIBE), size, error);
  writeVarint(writeBuf, subscribeRequest.subscribeID, size, error);
  writeVarint(writeBuf, subscribeRequest.trackAlias, size, error);
  writeFullTrackName(writeBuf, subscribeRequest.fullTrackName, size, error);
  writeBuf.append(&subscribeRequest.priority, 1);
  uint8_t order = folly::to_underlying(subscribeRequest.groupOrder);
  writeBuf.append(&order, 1);
  writeVarint(
      writeBuf, folly::to_underlying(subscribeRequest.locType), size, error);
  if (subscribeRequest.locType == LocationType::AbsoluteStart ||
      subscribeRequest.locType == LocationType::AbsoluteRange) {
    writeVarint(writeBuf, subscribeRequest.start->group, size, error);
    writeVarint(writeBuf, subscribeRequest.start->object, size, error);
  }
  if (subscribeRequest.locType == LocationType::AbsoluteRange) {
    writeVarint(writeBuf, subscribeRequest.end->group, size, error);
    writeVarint(writeBuf, subscribeRequest.end->object, size, error);
  }
  writeVarint(writeBuf, subscribeRequest.params.size(), size, error);
  for (auto& param : subscribeRequest.params) {
    writeVarint(writeBuf, param.key, size, error);
    writeFixedString(writeBuf, param.value, size, error);
  }
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  return size;
}

WriteResult writeSubscribeUpdate(
    folly::IOBufQueue& writeBuf,
    const SubscribeUpdate& update) noexcept {
  size_t size = 0;
  bool error = false;
  writeVarint(
      writeBuf, folly::to_underlying(FrameType::SUBSCRIBE_UPDATE), size, error);
  writeVarint(writeBuf, update.subscribeID, size, error);
  writeVarint(writeBuf, update.start.group, size, error);
  writeVarint(writeBuf, update.start.object, size, error);
  writeVarint(writeBuf, update.end.group, size, error);
  writeVarint(writeBuf, update.end.object, size, error);
  writeBuf.append(&update.priority, 1);
  writeVarint(writeBuf, update.params.size(), size, error);
  for (auto& param : update.params) {
    writeVarint(writeBuf, param.key, size, error);
    writeFixedString(writeBuf, param.value, size, error);
  }
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  return size;
}

WriteResult writeSubscribeOk(
    folly::IOBufQueue& writeBuf,
    const SubscribeOk& subscribeOk) noexcept {
  size_t size = 0;
  bool error = false;
  writeVarint(
      writeBuf, folly::to_underlying(FrameType::SUBSCRIBE_OK), size, error);
  writeVarint(writeBuf, subscribeOk.subscribeID, size, error);
  writeVarint(writeBuf, subscribeOk.expires.count(), size, error);
  auto order = folly::to_underlying(subscribeOk.groupOrder);
  writeBuf.append(&order, 1);
  if (subscribeOk.latest) {
    writeVarint(writeBuf, 1, size, error); // content exists
    writeVarint(writeBuf, subscribeOk.latest->group, size, error);
    writeVarint(writeBuf, subscribeOk.latest->object, size, error);
  } else {
    writeVarint(writeBuf, 0, size, error); // content exists
  }
  writeVarint(writeBuf, subscribeOk.params.size(), size, error);
  for (auto& param : subscribeOk.params) {
    writeVarint(writeBuf, param.key, size, error);
    writeFixedString(writeBuf, param.value, size, error);
  }
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  return size;
}

WriteResult writeSubscribeError(
    folly::IOBufQueue& writeBuf,
    const SubscribeError& subscribeError) noexcept {
  size_t size = 0;
  bool error = false;
  writeVarint(
      writeBuf, folly::to_underlying(FrameType::SUBSCRIBE_ERROR), size, error);
  writeVarint(writeBuf, subscribeError.subscribeID, size, error);
  writeVarint(writeBuf, subscribeError.errorCode, size, error);
  writeFixedString(writeBuf, subscribeError.reasonPhrase, size, error);
  writeVarint(writeBuf, subscribeError.retryAlias.value_or(0), size, error);
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  return size;
}

WriteResult writeUnsubscribe(
    folly::IOBufQueue& writeBuf,
    const Unsubscribe& unsubscribe) noexcept {
  size_t size = 0;
  bool error = false;
  writeVarint(
      writeBuf, folly::to_underlying(FrameType::UNSUBSCRIBE), size, error);
  writeVarint(writeBuf, unsubscribe.subscribeID, size, error);
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  return size;
}

WriteResult writeSubscribeDone(
    folly::IOBufQueue& writeBuf,
    const SubscribeDone& subscribeDone) noexcept {
  size_t size = 0;
  bool error = false;
  writeVarint(
      writeBuf, folly::to_underlying(FrameType::SUBSCRIBE_DONE), size, error);
  writeVarint(writeBuf, subscribeDone.subscribeID, size, error);
  writeVarint(
      writeBuf, folly::to_underlying(subscribeDone.statusCode), size, error);
  writeFixedString(writeBuf, subscribeDone.reasonPhrase, size, error);
  if (subscribeDone.finalObject) {
    writeVarint(writeBuf, 1, size, error);
    writeVarint(writeBuf, subscribeDone.finalObject->group, size, error);
    writeVarint(writeBuf, subscribeDone.finalObject->object, size, error);
  } else {
    writeVarint(writeBuf, 0, size, error);
  }
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  return size;
}

WriteResult writeAnnounce(
    folly::IOBufQueue& writeBuf,
    const Announce& announce) noexcept {
  size_t size = 0;
  bool error = false;
  writeVarint(writeBuf, folly::to_underlying(FrameType::ANNOUNCE), size, error);
  writeFixedString(writeBuf, announce.trackNamespace, size, error);
  writeVarint(writeBuf, announce.params.size(), size, error);
  for (auto& param : announce.params) {
    writeVarint(writeBuf, param.key, size, error);
    writeFixedString(writeBuf, param.value, size, error);
  }
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  return size;
}

WriteResult writeAnnounceOk(
    folly::IOBufQueue& writeBuf,
    const AnnounceOk& announceOk) noexcept {
  size_t size = 0;
  bool error = false;
  writeVarint(
      writeBuf, folly::to_underlying(FrameType::ANNOUNCE_OK), size, error);
  writeFixedString(writeBuf, announceOk.trackNamespace, size, error);
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  return size;
}

WriteResult writeAnnounceError(
    folly::IOBufQueue& writeBuf,
    const AnnounceError& announceError) noexcept {
  size_t size = 0;
  bool error = false;
  writeVarint(
      writeBuf, folly::to_underlying(FrameType::ANNOUNCE_ERROR), size, error);
  writeFixedString(writeBuf, announceError.trackNamespace, size, error);
  writeVarint(writeBuf, announceError.errorCode, size, error);
  writeFixedString(writeBuf, announceError.reasonPhrase, size, error);
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  return size;
}

WriteResult writeUnannounce(
    folly::IOBufQueue& writeBuf,
    const Unannounce& unannounce) noexcept {
  size_t size = 0;
  bool error = false;
  writeVarint(
      writeBuf, folly::to_underlying(FrameType::UNANNOUNCE), size, error);
  writeFixedString(writeBuf, unannounce.trackNamespace, size, error);
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  return size;
}

WriteResult writeAnnounceCancel(
    folly::IOBufQueue& writeBuf,
    const AnnounceCancel& announceCancel) noexcept {
  size_t size = 0;
  bool error = false;
  writeVarint(
      writeBuf, folly::to_underlying(FrameType::ANNOUNCE_CANCEL), size, error);
  writeFixedString(writeBuf, announceCancel.trackNamespace, size, error);
  writeVarint(writeBuf, announceCancel.errorCode, size, error);
  writeFixedString(writeBuf, announceCancel.reasonPhrase, size, error);
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  return size;
}

WriteResult writeTrackStatusRequest(
    folly::IOBufQueue& writeBuf,
    const TrackStatusRequest& trackStatusRequest) noexcept {
  size_t size = 0;
  bool error = false;
  writeVarint(
      writeBuf,
      folly::to_underlying(FrameType::TRACK_STATUS_REQUEST),
      size,
      error);
  writeFullTrackName(writeBuf, trackStatusRequest.fullTrackName, size, error);
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  return size;
}

WriteResult writeTrackStatus(
    folly::IOBufQueue& writeBuf,
    const TrackStatus& trackStatus) noexcept {
  size_t size = 0;
  bool error = false;
  writeVarint(
      writeBuf, folly::to_underlying(FrameType::TRACK_STATUS), size, error);
  writeFullTrackName(writeBuf, trackStatus.fullTrackName, size, error);
  writeVarint(
      writeBuf, folly::to_underlying(trackStatus.statusCode), size, error);
  if (trackStatus.statusCode == TrackStatusCode::IN_PROGRESS) {
    writeVarint(writeBuf, trackStatus.latestGroupAndObject->group, size, error);
    writeVarint(
        writeBuf, trackStatus.latestGroupAndObject->object, size, error);
  } else {
    writeVarint(writeBuf, 0, size, error);
    writeVarint(writeBuf, 0, size, error);
  }
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  return size;
}

WriteResult writeGoaway(
    folly::IOBufQueue& writeBuf,
    const Goaway& goaway) noexcept {
  size_t size = 0;
  bool error = false;
  writeVarint(writeBuf, folly::to_underlying(FrameType::GOAWAY), size, error);
  writeFixedString(writeBuf, goaway.newSessionUri, size, error);
  if (error) {
    return folly::makeUnexpected(quic::TransportErrorCode::INTERNAL_ERROR);
  }
  return size;
}

const char* getFrameTypeString(FrameType type) {
  switch (type) {
    case FrameType::OBJECT_STREAM:
      return "OBJECT_STREAM";
    case FrameType::OBJECT_DATAGRAM:
      return "OBJECT_DATAGRAM";
    case FrameType::CLIENT_SETUP:
      return "CLIENT_SETUP";
    case FrameType::SERVER_SETUP:
      return "SERVER_SETUP";
    case FrameType::SUBSCRIBE:
      return "SUBSCRIBE";
    case FrameType::SUBSCRIBE_OK:
      return "SUBSCRIBE_OK";
    case FrameType::SUBSCRIBE_ERROR:
      return "SUBSCRIBE_ERROR";
    case FrameType::SUBSCRIBE_DONE:
      return "SUBSCRIBE_DONE";
    case FrameType::UNSUBSCRIBE:
      return "UNSUBSCRIBE";
    case FrameType::ANNOUNCE:
      return "ANNOUNCE";
    case FrameType::ANNOUNCE_OK:
      return "ANNOUNCE_OK";
    case FrameType::ANNOUNCE_ERROR:
      return "ANNOUNCE_ERROR";
    case FrameType::UNANNOUNCE:
      return "UNANNOUNCE";
    case FrameType::GOAWAY:
      return "GOAWAY";
    case FrameType::STREAM_HEADER_TRACK:
      return "STREAM_HEADER_TRACK";
    case FrameType::STREAM_HEADER_GROUP:
      return "STREAM_HEADER_GROUP";
    default:
      // can happen when type was cast from uint8_t
      return "Unknown";
  }
  LOG(FATAL) << "Unreachable";
  return "";
}

std::ostream& operator<<(std::ostream& os, FrameType type) {
  os << getFrameTypeString(type);
  return os;
}

} // namespace moxygen
