// Drop-in for the iPhone app: rings through CallKit when the Gotchi calls,
// and joins the LiveKit room when you answer.
//
// Needs: LiveKit Swift SDK (https://github.com/livekit/client-sdk-swift, 2.x),
// Background Modes "Voice over IP" + "Audio", the Push Notifications capability,
// and NSMicrophoneUsageDescription in Info.plist.
//
// Start it once at launch (e.g. in your App init or AppDelegate):
//     GotchiCallManager.shared.start()

import AVFoundation
import CallKit
import Foundation
import LiveKit
import PushKit

final class GotchiCallManager: NSObject {
    static let shared = GotchiCallManager()

    // Same server and APP_SECRET as in the call server's settings.
    private let serverURL = URL(string: "https://your-call-server.vercel.app")!
    private let appSecret = "same-as-APP_SECRET-on-the-server"

    private struct PendingCall {
        let uuid: UUID
        let callId: String
        let url: String
        let token: String
    }

    private let pushRegistry = PKPushRegistry(queue: .main)
    private let provider: CXProvider
    private let callController = CXCallController()
    private var calls: [UUID: PendingCall] = [:]
    private var room: Room?

    private override init() {
        let config = CXProviderConfiguration()
        config.supportsVideo = false
        config.maximumCallsPerCallGroup = 1
        config.supportedHandleTypes = [.generic]
        provider = CXProvider(configuration: config)
        super.init()
        provider.setDelegate(self, queue: .main)
    }

    func start() {
        pushRegistry.delegate = self
        pushRegistry.desiredPushTypes = [.voIP]
    }

    private func call(withCallId callId: String) -> PendingCall? {
        calls.values.first { $0.callId == callId }
    }

    /// Tells the server, which deletes the room so the Gotchi hangs up too.
    private func notifyServerEnded(_ callId: String) {
        var request = URLRequest(url: serverURL.appendingPathComponent("api/call/decline"))
        request.httpMethod = "POST"
        request.setValue("Bearer \(appSecret)", forHTTPHeaderField: "Authorization")
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = try? JSONSerialization.data(withJSONObject: ["callId": callId])
        URLSession.shared.dataTask(with: request).resume()
    }
}

// MARK: - PushKit

extension GotchiCallManager: PKPushRegistryDelegate {
    func pushRegistry(_ registry: PKPushRegistry, didUpdate credentials: PKPushCredentials, for type: PKPushType) {
        // Put this value in the server's IPHONE_VOIP_TOKEN setting.
        let token = credentials.token.map { String(format: "%02x", $0) }.joined()
        print("Gotchi VoIP push token: \(token)")
    }

    func pushRegistry(_ registry: PKPushRegistry, didReceiveIncomingPushWith payload: PKPushPayload,
                      for type: PKPushType, completion: @escaping () -> Void) {
        let data = payload.dictionaryPayload
        let callId = data["callId"] as? String ?? ""

        if data["type"] as? String == "end", let existing = call(withCallId: callId) {
            // The Gotchi cancelled before you answered.
            provider.reportCall(with: existing.uuid, endedAt: nil, reason: .remoteEnded)
            calls[existing.uuid] = nil
            completion()
            return
        }

        // iOS requires every VoIP push to report a call, even a stale "end".
        let uuid = UUID()
        let update = CXCallUpdate()
        update.remoteHandle = CXHandle(type: .generic, value: data["callerName"] as? String ?? "CGotchi")
        update.localizedCallerName = data["callerName"] as? String ?? "CGotchi"
        update.hasVideo = false
        provider.reportNewIncomingCall(with: uuid, update: update) { [weak self] error in
            guard let self, error == nil else { return completion() }
            if data["type"] as? String == "incoming",
               let url = data["url"] as? String, let token = data["token"] as? String {
                self.calls[uuid] = PendingCall(uuid: uuid, callId: callId, url: url, token: token)
            } else {
                self.provider.reportCall(with: uuid, endedAt: nil, reason: .remoteEnded)
            }
            completion()
        }
    }
}

// MARK: - CallKit

extension GotchiCallManager: CXProviderDelegate {
    func providerDidReset(_ provider: CXProvider) {
        Task { await room?.disconnect() }
        room = nil
        calls.removeAll()
    }

    func provider(_ provider: CXProvider, perform action: CXAnswerCallAction) {
        guard let call = calls[action.callUUID] else { return action.fail() }
        let room = Room(delegate: self)
        self.room = room
        Task {
            do {
                try await room.connect(url: call.url, token: call.token)
                try await room.localParticipant.setMicrophone(enabled: true)
                action.fulfill()
            } catch {
                print("Gotchi call join failed: \(error)")
                action.fail()
                notifyServerEnded(call.callId)
            }
        }
    }

    func provider(_ provider: CXProvider, perform action: CXEndCallAction) {
        if let call = calls.removeValue(forKey: action.callUUID) {
            notifyServerEnded(call.callId)
        }
        Task { await room?.disconnect() }
        room = nil
        action.fulfill()
    }

    func provider(_ provider: CXProvider, perform action: CXSetMutedCallAction) {
        Task { try? await room?.localParticipant.setMicrophone(enabled: !action.isMuted) }
        action.fulfill()
    }
}

// MARK: - LiveKit

extension GotchiCallManager: RoomDelegate {
    func room(_ room: Room, participantDidDisconnect participant: RemoteParticipant) {
        // The Gotchi hung up: end the CallKit call.
        DispatchQueue.main.async { [weak self] in
            guard let self, let uuid = self.calls.keys.first else { return }
            self.provider.reportCall(with: uuid, endedAt: nil, reason: .remoteEnded)
            self.calls[uuid] = nil
            Task { await self.room?.disconnect() }
            self.room = nil
        }
    }

    func room(_ room: Room, didDisconnectWithError error: LiveKitError?) {
        DispatchQueue.main.async { [weak self] in
            guard let self, let uuid = self.calls.keys.first else { return }
            self.provider.reportCall(with: uuid, endedAt: nil, reason: .remoteEnded)
            self.calls[uuid] = nil
            self.room = nil
        }
    }
}
