import SwiftUI
import Combine
<<<<<<< HEAD

@MainActor
final class WSCarVM: ObservableObject {
    @Published var ip: String = "172.20.10.2"
    @Published var status: String = "未連線"
    @Published var speed: Double = 160

    // 當前控制狀態
    @Published var throttle: Int8 = 0   // -1,0,1
    @Published var steer: Int8 = 0      // -1,0,1
=======
import Foundation
import CoreBluetooth

final class BLECarTransport: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    @Published var status: String = "BLE 未連線"

    private let serviceUUID = CBUUID(string: "FFE0")
    private let charUUID = CBUUID(string: "FFE1")
    private let targetName = "ESP32car-BLE"

    private var central: CBCentralManager!
    private var targetPeripheral: CBPeripheral?
    private var writeChar: CBCharacteristic?
    private var scanTimeoutTask: DispatchWorkItem?

    var isReady: Bool { writeChar != nil && targetPeripheral != nil }

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }

    func connect() {
        guard central.state == .poweredOn else {
            status = "BLE 不可用（state=\(central.state.rawValue)）"
            return
        }

        // 不要先 disconnect，避免打斷初始化狀態
        scanTimeoutTask?.cancel()
        central.stopScan()

        status = "BLE 掃描中..."
        central.scanForPeripherals(
            withServices: nil,
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )

        let task = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.central.stopScan()
            if self.targetPeripheral == nil {
                self.status = "BLE 找不到 \(self.targetName)"
            }
        }
        scanTimeoutTask = task
        DispatchQueue.main.asyncAfter(deadline: .now() + 8, execute: task)
    }

    func disconnect() {
        scanTimeoutTask?.cancel()
        scanTimeoutTask = nil
        central.stopScan()

        if let p = targetPeripheral {
            central.cancelPeripheralConnection(p)
        }

        writeChar = nil
        targetPeripheral = nil
        status = "BLE 已斷線"
    }

    func send(_ data: Data) {
        guard let p = targetPeripheral, let c = writeChar else { return }
        p.writeValue(data, for: c, type: .withoutResponse)
    }

    // MARK: - CBCentralManagerDelegate
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            status = "BLE 可用"
        case .poweredOff:
            status = "BLE 已關閉（請到 iPad 設定開啟藍牙）"
        case .unauthorized:
            status = "BLE 權限未允許（請到 設定 > 隱私權與安全性 > 藍牙 開啟本 App）"
        case .unsupported:
            status = "此裝置不支援 BLE"
        case .resetting:
            status = "BLE 重置中"
        case .unknown:
            status = "BLE 狀態未知"
        @unknown default:
            status = "BLE 狀態異常"
        }
    }

    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String : Any],
                        rssi RSSI: NSNumber) {
        let advName = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? ""
        let pName = peripheral.name ?? ""
        let matched = advName.contains(targetName) || pName.contains(targetName)
        guard matched else { return }

        scanTimeoutTask?.cancel()
        scanTimeoutTask = nil

        status = "BLE 連線中..."
        targetPeripheral = peripheral
        peripheral.delegate = self
        central.stopScan()
        central.connect(peripheral, options: nil)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        status = "BLE 已連線，探索服務..."
        peripheral.discoverServices([serviceUUID])
    }

    func centralManager(_ central: CBCentralManager,
                        didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        writeChar = nil
        targetPeripheral = nil
        status = "BLE 連線失敗: \(error?.localizedDescription ?? "unknown")"
    }

    func centralManager(_ central: CBCentralManager,
                        didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        writeChar = nil
        targetPeripheral = nil
        status = "BLE 已斷線"
    }

    // MARK: - CBPeripheralDelegate
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil, let services = peripheral.services else {
            status = "BLE 服務探索失敗"
            return
        }

        for s in services where s.uuid == serviceUUID {
            peripheral.discoverCharacteristics([charUUID], for: s)
        }
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        guard error == nil, let chars = service.characteristics else {
            status = "BLE 特徵探索失敗"
            return
        }

        for c in chars where c.uuid == charUUID {
            let writable = c.properties.contains(.writeWithoutResponse) || c.properties.contains(.write)
            guard writable else {
                status = "BLE 特徵不可寫入"
                return
            }

            writeChar = c
            status = "BLE 可控制"
            return
        }

        status = "BLE 找不到可寫入特徵"
    }
}

@MainActor
final class WSCarVM: ObservableObject {
    enum LinkMode: String, CaseIterable, Identifiable {
        case ws = "Wi‑Fi"
        case ble = "BLE"
        var id: String { rawValue }
    }

    @Published var mode: LinkMode = .ws
    @Published var ip: String = "172.20.10.2"
    @Published var status: String = "未連線"
    @Published var speed: Double = 160
    @Published var throttle: Int8 = 0
    @Published var steer: Int8 = 0
>>>>>>> babc3f4 (Update BLE permission and iOS fixes)

    private var task: URLSessionWebSocketTask?
    private let session = URLSession(configuration: .default)
    private var ticker: AnyCancellable?
    private var seq: UInt16 = 0
<<<<<<< HEAD

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
=======
    private let sendInterval: TimeInterval = 1.0 / 30.0

    let ble = BLECarTransport()

    func connect() {
        disconnect()

        switch mode {
        case .ws:
            guard let url = URL(string: "ws://\(ip):81/") else {
                status = "IP 格式錯誤"
                return
            }
            task = session.webSocketTask(with: url)
            task?.resume()
            status = "WS 連線中..."
            receiveLoop()
            startTicker()

        case .ble:
            status = ble.status
            ble.connect()
            startTicker()
        }
>>>>>>> babc3f4 (Update BLE permission and iOS fixes)
    }

    func disconnect() {
        stopTicker()
        task?.cancel(with: .goingAway, reason: nil)
        task = nil
<<<<<<< HEAD
=======
        ble.disconnect()
>>>>>>> babc3f4 (Update BLE permission and iOS fixes)
        status = "已斷線"
    }

    private func startTicker() {
        ticker = Timer.publish(every: sendInterval, on: .main, in: .common)
            .autoconnect()
            .sink { [weak self] _ in
                self?.sendStatePacket()
<<<<<<< HEAD
=======
                self?.syncStatus()
>>>>>>> babc3f4 (Update BLE permission and iOS fixes)
            }
    }

    private func stopTicker() {
        ticker?.cancel()
        ticker = nil
    }

<<<<<<< HEAD
    private func sendStatePacket() {
        guard let task else {
            status = "尚未連線"
            return
        }

        seq &+= 1
        let spd = UInt8(max(0, min(255, Int(speed))))

        // 封包：seq(2) + throttle(1) + steer(1) + speed(1) = 5 bytes
=======
    private func makePacketData() -> Data {
        seq &+= 1
        let spd = UInt8(max(0, min(255, Int(speed))))
>>>>>>> babc3f4 (Update BLE permission and iOS fixes)
        var data = Data()
        var seqLE = seq.littleEndian
        withUnsafeBytes(of: &seqLE) { data.append(contentsOf: $0) }
        data.append(UInt8(bitPattern: throttle))
        data.append(UInt8(bitPattern: steer))
        data.append(spd)
<<<<<<< HEAD

        task.send(.data(data)) { [weak self] error in
            if let error {
                DispatchQueue.main.async {
                    self?.status = "送出失敗: \(error.localizedDescription)"
                }
            }
        }
    }

=======
        return data
    }

    private func sendStatePacket() {
        let data = makePacketData()

        switch mode {
        case .ws:
            guard let task else {
                status = "WS 尚未連線"
                return
            }
            task.send(.data(data)) { [weak self] error in
                if let error {
                    DispatchQueue.main.async {
                        self?.status = "WS 送出失敗: \(error.localizedDescription)"
                    }
                }
            }

        case .ble:
            // 關鍵：BLE 尚未 ready 不送，避免誤判
            guard ble.isReady else { return }
            ble.send(data)
        }
    }

    private func syncStatus() {
        if mode == .ble { status = ble.status }
    }

>>>>>>> babc3f4 (Update BLE permission and iOS fixes)
    private func receiveLoop() {
        task?.receive { [weak self] result in
            DispatchQueue.main.async {
                switch result {
                case .failure(let e):
<<<<<<< HEAD
                    self?.status = "連線錯誤: \(e.localizedDescription)"
                case .success(let msg):
                    if case .string(let s) = msg {
                        self?.status = "ESP32: \(s)"
                    }
=======
                    self?.status = "WS 連線錯誤: \(e.localizedDescription)"
                case .success(let msg):
                    if case .string(let s) = msg { self?.status = "ESP32: \(s)" }
>>>>>>> babc3f4 (Update BLE permission and iOS fixes)
                    self?.receiveLoop()
                }
            }
        }
    }

<<<<<<< HEAD
    // UI 事件：只改狀態，不直接發包（由ticker固定頻率發送）
    func pressForward() { throttle = 1 }
    func pressBackward() { throttle = -1 }
    func releaseThrottle() { throttle = 0 }

=======
    func pressForward() { throttle = 1 }
    func pressBackward() { throttle = -1 }
    func releaseThrottle() { throttle = 0 }
>>>>>>> babc3f4 (Update BLE permission and iOS fixes)
    func pressLeft() { steer = -1 }
    func pressRight() { steer = 1 }
    func releaseSteer() { steer = 0 }

    func emergencyStop() {
        throttle = 0
        steer = 0
<<<<<<< HEAD
        sendStatePacket() // 立刻補一包 stop
=======
        sendStatePacket()
>>>>>>> babc3f4 (Update BLE permission and iOS fixes)
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
<<<<<<< HEAD
                        if !pressed {
                            pressed = true
                            press()
                        }
=======
                        if !pressed { pressed = true; press() }
>>>>>>> babc3f4 (Update BLE permission and iOS fixes)
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
<<<<<<< HEAD
            Text("ESP32 WebSocket Car")
                .font(.largeTitle).bold()

            HStack {
                TextField("192.168.1.88", text: $vm.ip)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 220)
                Button("連線") { vm.connect() }
                Button("斷線") { vm.disconnect() }
=======
            Text("ESP32 Car Controller").font(.largeTitle).bold()

            Picker("連線模式", selection: $vm.mode) {
                ForEach(WSCarVM.LinkMode.allCases) { m in
                    Text(m.rawValue).tag(m)
                }
            }
            .pickerStyle(.segmented)
            .frame(width: 260)

            if vm.mode == .ws {
                HStack {
                    TextField("192.168.1.88", text: $vm.ip)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 220)
                    Button("連線") { vm.connect() }
                    Button("斷線") { vm.disconnect() }
                }
            } else {
                HStack {
                    Button("BLE 連線") { vm.connect() }
                    Button("斷線") { vm.disconnect() }
                }
>>>>>>> babc3f4 (Update BLE permission and iOS fixes)
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
