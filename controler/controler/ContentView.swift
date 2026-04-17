import SwiftUI
import Combine

@MainActor
final class WSCarVM: ObservableObject {
    @Published var ip: String = "172.20.10.2"
    @Published var status: String = "未連線"
    @Published var speed: Double = 160

    // 當前控制狀態
    @Published var throttle: Int8 = 0   // -1,0,1
    @Published var steer: Int8 = 0      // -1,0,1

    private var task: URLSessionWebSocketTask?
    private let session = URLSession(configuration: .default)
    private var ticker: AnyCancellable?
    private var seq: UInt16 = 0

    private let sendInterval: TimeInterval = 1.0 / 30.0

    func connect() {
        disconnect()

        guard let url = URL(string: "ws://\(ip):81/") else {
            status = "IP 格式錯誤"
            return
        }

        task = session.webSocketTask(with: url)
        task?.resume()
        status = "連線中..."
        receiveLoop()
        startTicker()
    }

    func disconnect() {
        stopTicker()
        task?.cancel(with: .goingAway, reason: nil)
        task = nil
        status = "已斷線"
    }

    private func startTicker() {
        ticker = Timer.publish(every: sendInterval, on: .main, in: .common)
            .autoconnect()
            .sink { [weak self] _ in
                self?.sendStatePacket()
            }
    }

    private func stopTicker() {
        ticker?.cancel()
        ticker = nil
    }

    private func sendStatePacket() {
        guard let task else {
            status = "尚未連線"
            return
        }

        seq &+= 1
        let spd = UInt8(max(0, min(255, Int(speed))))

        // 封包：seq(2) + throttle(1) + steer(1) + speed(1) = 5 bytes
        var data = Data()
        var seqLE = seq.littleEndian
        withUnsafeBytes(of: &seqLE) { data.append(contentsOf: $0) }
        data.append(UInt8(bitPattern: throttle))
        data.append(UInt8(bitPattern: steer))
        data.append(spd)

        task.send(.data(data)) { [weak self] error in
            if let error {
                DispatchQueue.main.async {
                    self?.status = "送出失敗: \(error.localizedDescription)"
                }
            }
        }
    }

    private func receiveLoop() {
        task?.receive { [weak self] result in
            DispatchQueue.main.async {
                switch result {
                case .failure(let e):
                    self?.status = "連線錯誤: \(e.localizedDescription)"
                case .success(let msg):
                    if case .string(let s) = msg {
                        self?.status = "ESP32: \(s)"
                    }
                    self?.receiveLoop()
                }
            }
        }
    }

    // UI 事件：只改狀態，不直接發包（由ticker固定頻率發送）
    func pressForward() { throttle = 1 }
    func pressBackward() { throttle = -1 }
    func releaseThrottle() { throttle = 0 }

    func pressLeft() { steer = -1 }
    func pressRight() { steer = 1 }
    func releaseSteer() { steer = 0 }

    func emergencyStop() {
        throttle = 0
        steer = 0
        sendStatePacket() // 立刻補一包 stop
    }
}

struct HoldButton: View {
    let title: String
    let press: () -> Void
    let release: () -> Void
    @State private var pressed = false

    var body: some View {
        Text(title)
            .font(.system(size: 34, weight: .bold))
            .frame(width: 110, height: 90)
            .background(Color.gray.opacity(0.2))
            .cornerRadius(14)
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in
                        if !pressed {
                            pressed = true
                            press()
                        }
                    }
                    .onEnded { _ in
                        pressed = false
                        release()
                    }
            )
    }
}

struct ContentView: View {
    @StateObject private var vm = WSCarVM()

    var body: some View {
        VStack(spacing: 16) {
            Text("ESP32 WebSocket Car")
                .font(.largeTitle).bold()

            HStack {
                TextField("192.168.1.88", text: $vm.ip)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 220)
                Button("連線") { vm.connect() }
                Button("斷線") { vm.disconnect() }
            }

            HStack {
                Text("速度 \(Int(vm.speed))")
                Slider(value: $vm.speed, in: 0...255, step: 1).frame(width: 220)
            }

            Text(vm.status).foregroundStyle(.secondary)

            HoldButton(title: "↑") { vm.pressForward() } release: { vm.releaseThrottle() }

            HStack(spacing: 20) {
                HoldButton(title: "←") { vm.pressLeft() } release: { vm.releaseSteer() }

                Button("STOP") { vm.emergencyStop() }
                    .font(.title2.bold())
                    .frame(width: 120, height: 90)
                    .background(Color.red.opacity(0.2))
                    .cornerRadius(14)

                HoldButton(title: "→") { vm.pressRight() } release: { vm.releaseSteer() }
            }

            HoldButton(title: "↓") { vm.pressBackward() } release: { vm.releaseThrottle() }
        }
        .padding()
    }
}
