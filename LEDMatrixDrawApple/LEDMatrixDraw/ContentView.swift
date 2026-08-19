import Network
import PhotosUI
import SwiftUI
import UIKit

private let matrixSize = 64
private let developmentTLSFallbackHosts: Set<String> = []
private let defaultBackendURL = ""

struct RGB: Codable, Equatable {
    var r: UInt8
    var g: UInt8
    var b: UInt8

    static let black = RGB(r: 0, g: 0, b: 0)

    var color: Color {
        Color(red: Double(r) / 255, green: Double(g) / 255, blue: Double(b) / 255)
    }

    init(r: UInt8, g: UInt8, b: UInt8) {
        self.r = r
        self.g = g
        self.b = b
    }

    var isOff: Bool {
        r == 0 && g == 0 && b == 0
    }

    init(_ color: Color) {
        let resolved = UIColor(color)
        var red: CGFloat = 0
        var green: CGFloat = 0
        var blue: CGFloat = 0
        var alpha: CGFloat = 0
        resolved.getRed(&red, green: &green, blue: &blue, alpha: &alpha)
        r = UInt8(max(0, min(255, Int(red * 255))))
        g = UInt8(max(0, min(255, Int(green * 255))))
        b = UInt8(max(0, min(255, Int(blue * 255))))
    }
}

enum DrawTool: String, CaseIterable, Identifiable {
    case brush = "Brush"
    case eraser = "Erase"
    case fill = "Fill"
    case line = "Line"
    case rect = "Rect"
    case circle = "Ellipse"
    case text = "Text"

    var id: String { rawValue }
}

struct MatrixPayload: Codable {
    let ok: Bool
    let hasFrame: Bool
    let width: Int
    let height: Int
    let frameHex: String
    let brightness: Int
    let sequence: Int
    let updatedAt: String
}

final class BackendSessionDelegate: NSObject, URLSessionDelegate {
    func urlSession(
        _ session: URLSession,
        didReceive challenge: URLAuthenticationChallenge,
        completionHandler: @escaping (URLSession.AuthChallengeDisposition, URLCredential?) -> Void
    ) {
        if challenge.protectionSpace.authenticationMethod == NSURLAuthenticationMethodServerTrust,
           let trust = challenge.protectionSpace.serverTrust {
            let host = challenge.protectionSpace.host as CFString
            SecTrustSetPolicies(trust, SecPolicyCreateSSL(true, host))

            var error: CFError?
            if SecTrustEvaluateWithError(trust, &error) {
                completionHandler(.useCredential, URLCredential(trust: trust))
            } else if developmentTLSFallbackHosts.contains(challenge.protectionSpace.host.lowercased()) {
                completionHandler(.useCredential, URLCredential(trust: trust))
            } else {
                completionHandler(.cancelAuthenticationChallenge, nil)
            }
        } else {
            completionHandler(.performDefaultHandling, nil)
        }
    }
}

private let backendSession = URLSession(
    configuration: {
        let configuration = URLSessionConfiguration.default
        configuration.waitsForConnectivity = true
        configuration.timeoutIntervalForRequest = 12
        configuration.timeoutIntervalForResource = 18
        return configuration
    }(),
    delegate: BackendSessionDelegate(),
    delegateQueue: nil
)

final class LocalNetworkPermissionRequester {
    static let shared = LocalNetworkPermissionRequester()

    private var browser: NWBrowser?
    private let queue = DispatchQueue(label: "DrawAnywhereLocalNetworkPermission")

    func request() {
        guard browser == nil else { return }
        let browser = NWBrowser(for: .bonjour(type: "_http._tcp", domain: nil), using: .tcp)
        browser.stateUpdateHandler = { state in
            if case .failed = state {
                browser.cancel()
            }
        }
        browser.start(queue: queue)
        self.browser = browser
    }
}

private let pixelFont: [Character: [String]] = [
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "C": ["01111", "10000", "10000", "10000", "10000", "10000", "01111"],
    "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    "G": ["01111", "10000", "10000", "10011", "10001", "10001", "01111"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "I": ["11111", "00100", "00100", "00100", "00100", "00100", "11111"],
    "J": ["00111", "00010", "00010", "00010", "10010", "10010", "01100"],
    "K": ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    "N": ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "Q": ["01110", "10001", "10001", "10001", "10101", "10010", "01101"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
    "W": ["10001", "10001", "10001", "10101", "10101", "10101", "01010"],
    "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    "Z": ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    "3": ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    "4": ["10010", "10010", "10010", "11111", "00010", "00010", "00010"],
    "5": ["11111", "10000", "10000", "11110", "00001", "00001", "11110"],
    "6": ["01110", "10000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00001", "01110"],
    ".": ["00000", "00000", "00000", "00000", "00000", "01100", "01100"],
    "!": ["00100", "00100", "00100", "00100", "00100", "00000", "00100"],
    "?": ["01110", "10001", "00001", "00010", "00100", "00000", "00100"],
    "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
    "_": ["00000", "00000", "00000", "00000", "00000", "00000", "11111"],
    ":": ["00000", "01100", "01100", "00000", "01100", "01100", "00000"],
    "/": ["00001", "00010", "00010", "00100", "01000", "01000", "10000"],
    " ": ["00000", "00000", "00000", "00000", "00000", "00000", "00000"]
]

struct ContentView: View {
    @AppStorage("backendURL") private var backendURL = defaultBackendURL
    @AppStorage("darkMode") private var darkMode = false

    @State private var pixels = Array(repeating: RGB.black, count: matrixSize * matrixSize)
    @State private var selectedColor = Color(red: 1, green: 0.19, blue: 0.35)
    @State private var selectedTool = DrawTool.brush
    @State private var brushSize = 1.0
    @AppStorage("brightness") private var brightness = 100.0
    @State private var textValue = "Hi"
    @State private var textSize = 1.0
    @State private var status = "Ready"
    @State private var dragStart: Point?
    @State private var lastPaintPoint: Point?
    @State private var showingSettings = false
    @State private var selectedPhoto: PhotosPickerItem?
    @State private var imagePickerStatus = "Import photo to 64x64"
    @State private var undoStack: [[RGB]] = []
    @State private var redoStack: [[RGB]] = []
    @State private var previewPixels: [RGB]?
    @State private var imageSourcePixels: [RGB]?
    @State private var imagePosition = Point(x: 0, y: 0)
    @State private var imageSize = 64.0
    @State private var imageDragOffset: Point?
    @FocusState private var textFieldFocused: Bool

    private let columns = [GridItem(.adaptive(minimum: 82), spacing: 8)]

    var body: some View {
        NavigationStack {
            GeometryReader { proxy in
                let compact = proxy.size.width < 760
                Group {
                    if compact {
                        ScrollView {
                            VStack(spacing: 18) {
                                matrixEditor(showClearButton: true)
                                controls(showClearButton: false)
                            }
                            .padding()
                        }
                    } else {
                        HStack(spacing: 0) {
                            controls(showClearButton: true)
                                .frame(width: 310)
                                .padding()
                                .background(.regularMaterial)
                            matrixEditor(showClearButton: false)
                                .padding(24)
                        }
                    }
                }
            }
            .navigationTitle("Draw Anywhere")
            .toolbar {
                ToolbarItemGroup(placement: .topBarTrailing) {
                    Button("Undo") { undo() }
                        .disabled(undoStack.isEmpty)
                    Button("Redo") { redo() }
                        .disabled(redoStack.isEmpty)
                    Button("Settings") { showingSettings = true }
                    Button("Send") { sendToBackend() }
                        .buttonStyle(.borderedProminent)
                }
                ToolbarItemGroup(placement: .keyboard) {
                    Spacer()
                    Button("Done") { textFieldFocused = false }
                }
            }
            .sheet(isPresented: $showingSettings) {
                SettingsView(backendURL: $backendURL, darkMode: $darkMode)
            }
            .onChange(of: selectedPhoto) { _, item in
                importPhoto(item)
            }
            .onAppear {
                LocalNetworkPermissionRequester.shared.request()
                backendURL = normalizedURLString(backendURL)
                loadFromBackend()
            }
        }
    }

    private func controls(showClearButton: Bool) -> some View {
        VStack(alignment: .leading, spacing: 18) {
            HStack(spacing: 12) {
                BrandMark()
                VStack(alignment: .leading, spacing: 4) {
                    Text("Draw Anywhere")
                        .font(.headline)
                    Text(status)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            SectionBox(title: "Tools") {
                LazyVGrid(columns: columns, spacing: 8) {
                    ForEach(DrawTool.allCases) { tool in
                        Button(tool.rawValue) { selectedTool = tool }
                            .buttonStyle(.bordered)
                            .tint(selectedTool == tool ? .accentColor : .secondary)
                    }
                }
            }

            SectionBox(title: "Color") {
                ColorPicker("Selected color", selection: $selectedColor, supportsOpacity: false)
                VStack(alignment: .leading) {
                    Text("Brush size \(Int(brushSize))")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Slider(value: $brushSize, in: 1...8, step: 1)
                }
                VStack(alignment: .leading) {
                    Text("Brightness \(Int(brightness))%")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Slider(value: $brightness, in: 1...100, step: 1)
                }
            }

            SectionBox(title: "Text") {
                TextField("Text", text: $textValue, axis: .vertical)
                    .textFieldStyle(.roundedBorder)
                    .lineLimit(3...5)
                    .focused($textFieldFocused)
                Stepper("Scale \(Int(textSize))", value: $textSize, in: 1...4)
            }

            SectionBox(title: "Image") {
                PhotosPicker(selection: $selectedPhoto, matching: .images) {
                    HStack(spacing: 12) {
                        Image(systemName: "photo.on.rectangle.angled")
                            .font(.title3)
                            .frame(width: 38, height: 38)
                            .background(Color.accentColor.opacity(0.12))
                            .clipShape(RoundedRectangle(cornerRadius: 8))
                        VStack(alignment: .leading, spacing: 2) {
                            Text("Choose image")
                                .font(.subheadline)
                                .fontWeight(.semibold)
                            Text(imagePickerStatus)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        Spacer()
                        Image(systemName: "chevron.right")
                            .font(.caption)
                            .foregroundStyle(.tertiary)
                    }
                    .padding(12)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(Color.accentColor.opacity(0.06))
                    .overlay(
                        RoundedRectangle(cornerRadius: 8)
                            .stroke(Color.accentColor.opacity(0.35), style: StrokeStyle(lineWidth: 1, dash: [5, 4]))
                    )
                    .clipShape(RoundedRectangle(cornerRadius: 8))
                }
                .buttonStyle(.plain)
            }

            if showClearButton {
                clearButton
            }
        }
    }

    private func matrixEditor(showClearButton: Bool) -> some View {
        VStack(spacing: 14) {
            GeometryReader { geometry in
                let side = min(geometry.size.width, geometry.size.height)
                Canvas { context, size in
                    let cell = min(size.width, size.height) / CGFloat(matrixSize)
                    let displayedPixels = previewPixels ?? imageLayerPixels()
                    for y in 0..<matrixSize {
                        for x in 0..<matrixSize {
                            let pixel = displayedPixels[pixelIndex(x, y)]
                            if pixel.isOff { continue }
                            let rect = CGRect(x: CGFloat(x) * cell, y: CGFloat(y) * cell, width: cell, height: cell)
                            context.fill(Path(rect), with: .color(pixel.color))
                        }
                    }
                }
                .frame(width: side, height: side)
                .background(Color.black)
                .clipShape(RoundedRectangle(cornerRadius: 8))
                .overlay(RoundedRectangle(cornerRadius: 8).stroke(.quaternary))
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { value in
                            handleDragChanged(matrixPoint(value.location, side: side))
                        }
                        .onEnded { value in
                            handleDragEnded(matrixPoint(value.location, side: side))
                        }
                )
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            .aspectRatio(1, contentMode: .fit)

            Text("Tool: \(selectedTool.rawValue)")
                .font(.caption)
                .foregroundStyle(.secondary)

            if imageSourcePixels != nil {
                imagePlacementPopup
            }

            if showClearButton {
                clearButton
                    .frame(maxWidth: .infinity)
            }
        }
    }

    private var clearButton: some View {
        Button(role: .destructive) {
            recordHistory()
            previewPixels = nil
            imageSourcePixels = nil
            imageDragOffset = nil
            pixels = Array(repeating: .black, count: matrixSize * matrixSize)
            status = "Canvas cleared"
        } label: {
            Label("Clear canvas", systemImage: "trash")
                .frame(maxWidth: .infinity)
        }
        .buttonStyle(.bordered)
    }

    private var imagePlacementPopup: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Image size \(Int(imageSize))")
                .font(.caption)
                .foregroundStyle(.secondary)
            Slider(value: $imageSize, in: 4...64, step: 1)
                .onChange(of: imageSize) { _, _ in
                    if let source = imageSourcePixels {
                        previewPixels = compositeImageLayer(source: source)
                    }
                    status = "Resize image, drag to move, then place"
                }
            HStack {
                Button("Cancel") { cancelImageLayer() }
                    .buttonStyle(.bordered)
                Button("Place") { placeImageLayer() }
                    .buttonStyle(.borderedProminent)
            }
        }
        .padding(12)
        .frame(maxWidth: 360)
        .background(.regularMaterial)
        .clipShape(RoundedRectangle(cornerRadius: 8))
        .overlay(RoundedRectangle(cornerRadius: 8).stroke(.quaternary))
    }

    private func matrixPoint(_ location: CGPoint, side: CGFloat) -> Point {
        let side = max(1, side)
        let x = max(0, min(matrixSize - 1, Int((location.x / side) * CGFloat(matrixSize))))
        let y = max(0, min(matrixSize - 1, Int((location.y / side) * CGFloat(matrixSize))))
        return Point(x: x, y: y)
    }

    private func handleDragChanged(_ point: Point) {
        if let source = imageSourcePixels {
            if imageDragOffset == nil && pointInImageLayer(point) {
                imageDragOffset = Point(x: point.x - imagePosition.x, y: point.y - imagePosition.y)
                status = "Move image"
                return
            }
            if let offset = imageDragOffset {
                let size = Int(imageSize)
                imagePosition = Point(
                    x: max(-size + 1, min(matrixSize - 1, point.x - offset.x)),
                    y: max(-size + 1, min(matrixSize - 1, point.y - offset.y))
                )
                previewPixels = compositeImageLayer(source: source)
                return
            }
        }

        if dragStart == nil {
            recordHistory()
            dragStart = point
            lastPaintPoint = point
            if [.brush, .eraser, .fill, .text].contains(selectedTool) {
                applySinglePointTool(at: point)
            }
            return
        }

        if selectedTool == .brush || selectedTool == .eraser {
            let color = selectedTool == .eraser ? RGB.black : RGB(selectedColor)
            paintBrushStroke(from: lastPaintPoint ?? point, to: point, color: color)
            lastPaintPoint = point
        } else if [.line, .rect, .circle].contains(selectedTool), let start = dragStart {
            updateShapePreview(from: start, to: point)
        }
    }

    private func handleDragEnded(_ point: Point) {
        defer {
            dragStart = nil
            lastPaintPoint = nil
            previewPixels = imageSourcePixels == nil ? nil : previewPixels
            imageDragOffset = nil
        }
        if imageSourcePixels != nil { return }
        guard let start = dragStart else { return }
        let color = selectedTool == .eraser ? RGB.black : RGB(selectedColor)
        switch selectedTool {
        case .line:
            drawLine(from: start, to: point, color: color)
        case .rect:
            drawRect(from: start, to: point, color: color)
        case .circle:
            drawCircle(center: start, edge: point, color: color)
        default:
            break
        }
    }

    private func applySinglePointTool(at point: Point) {
        let color = selectedTool == .eraser ? RGB.black : RGB(selectedColor)
        switch selectedTool {
        case .brush, .eraser:
            paintBrush(at: point, color: color)
        case .fill:
            floodFill(at: point, color: color)
        case .text:
            drawText(at: point)
        default:
            break
        }
    }

    private func pixelIndex(_ x: Int, _ y: Int) -> Int {
        y * matrixSize + x
    }

    private func recordHistory() {
        undoStack.append(pixels)
        if undoStack.count > 100 {
            undoStack.removeFirst()
        }
        redoStack.removeAll()
    }

    private func undo() {
        guard let previous = undoStack.popLast() else { return }
        previewPixels = nil
        imageSourcePixels = nil
        imageDragOffset = nil
        redoStack.append(pixels)
        pixels = previous
        status = "Undo"
    }

    private func redo() {
        guard let next = redoStack.popLast() else { return }
        previewPixels = nil
        imageSourcePixels = nil
        imageDragOffset = nil
        undoStack.append(pixels)
        pixels = next
        status = "Redo"
    }

    private func isInside(_ point: Point) -> Bool {
        point.x >= 0 && point.x < matrixSize && point.y >= 0 && point.y < matrixSize
    }

    private func inBounds(_ x: Int, _ y: Int) -> Bool {
        x >= 0 && x < matrixSize && y >= 0 && y < matrixSize
    }

    private func setPixel(_ point: Point, _ color: RGB) {
        guard isInside(point) else { return }
        pixels[pixelIndex(point.x, point.y)] = color
    }

    private func setPixel(_ point: Point, _ color: RGB, in target: inout [RGB]) {
        guard isInside(point) else { return }
        target[pixelIndex(point.x, point.y)] = color
    }

    private func updateShapePreview(from start: Point, to end: Point) {
        var preview = pixels
        let color = RGB(selectedColor)
        switch selectedTool {
        case .line:
            drawLine(from: start, to: end, color: color, in: &preview)
        case .rect:
            drawRect(from: start, to: end, color: color, in: &preview)
        case .circle:
            drawCircle(center: start, edge: end, color: color, in: &preview)
        default:
            break
        }
        previewPixels = preview
    }

    private func paintBrush(at point: Point, color: RGB) {
        let size = Int(brushSize)
        let half = size / 2
        for y in (point.y - half)..<(point.y - half + size) {
            for x in (point.x - half)..<(point.x - half + size) {
                setPixel(Point(x: x, y: y), color)
            }
        }
    }

    private func paintBrushStroke(from start: Point, to end: Point, color: RGB) {
        let dx = end.x - start.x
        let dy = end.y - start.y
        let steps = max(abs(dx), abs(dy), 1)
        for step in 0...steps {
            let x = start.x + Int(round(Double(dx * step) / Double(steps)))
            let y = start.y + Int(round(Double(dy * step) / Double(steps)))
            paintBrush(at: Point(x: x, y: y), color: color)
        }
    }

    private func floodFill(at point: Point, color: RGB) {
        guard isInside(point) else { return }
        let target = pixels[pixelIndex(point.x, point.y)]
        guard target != color else { return }
        var queue = [point]
        while let current = queue.popLast() {
            guard isInside(current), pixels[pixelIndex(current.x, current.y)] == target else { continue }
            setPixel(current, color)
            queue.append(Point(x: current.x + 1, y: current.y))
            queue.append(Point(x: current.x - 1, y: current.y))
            queue.append(Point(x: current.x, y: current.y + 1))
            queue.append(Point(x: current.x, y: current.y - 1))
        }
    }

    private func drawLine(from start: Point, to end: Point, color: RGB) {
        drawLine(from: start, to: end, color: color, in: &pixels)
    }

    private func drawLine(from start: Point, to end: Point, color: RGB, in target: inout [RGB]) {
        var x0 = start.x
        var y0 = start.y
        let dx = abs(end.x - x0)
        let sx = x0 < end.x ? 1 : -1
        let dy = -abs(end.y - y0)
        let sy = y0 < end.y ? 1 : -1
        var error = dx + dy
        while true {
            setPixel(Point(x: x0, y: y0), color, in: &target)
            if x0 == end.x && y0 == end.y { break }
            let e2 = 2 * error
            if e2 >= dy {
                error += dy
                x0 += sx
            }
            if e2 <= dx {
                error += dx
                y0 += sy
            }
        }
    }

    private func drawRect(from start: Point, to end: Point, color: RGB) {
        drawRect(from: start, to: end, color: color, in: &pixels)
    }

    private func drawRect(from start: Point, to end: Point, color: RGB, in target: inout [RGB]) {
        let minX = min(start.x, end.x)
        let maxX = max(start.x, end.x)
        let minY = min(start.y, end.y)
        let maxY = max(start.y, end.y)
        for x in minX...maxX {
            setPixel(Point(x: x, y: minY), color, in: &target)
            setPixel(Point(x: x, y: maxY), color, in: &target)
        }
        for y in minY...maxY {
            setPixel(Point(x: minX, y: y), color, in: &target)
            setPixel(Point(x: maxX, y: y), color, in: &target)
        }
    }

    private func drawCircle(center: Point, edge: Point, color: RGB) {
        drawCircle(center: center, edge: edge, color: color, in: &pixels)
    }

    private func drawCircle(center: Point, edge: Point, color: RGB, in target: inout [RGB]) {
        let minX = min(center.x, edge.x)
        let maxX = max(center.x, edge.x)
        let minY = min(center.y, edge.y)
        let maxY = max(center.y, edge.y)
        let rx = max(1.0, Double(maxX - minX) / 2.0)
        let ry = max(1.0, Double(maxY - minY) / 2.0)
        let cx = Double(minX + maxX) / 2.0
        let cy = Double(minY + maxY) / 2.0
        let steps = max(16, Int(ceil(2.0 * Double.pi * max(rx, ry) * 1.4)))
        for step in 0..<steps {
            let angle = (Double(step) / Double(steps)) * Double.pi * 2.0
            let x = Int(round(cx + cos(angle) * rx))
            let y = Int(round(cy + sin(angle) * ry))
            setPixel(Point(x: x, y: y), color, in: &target)
        }
    }

    private func drawText(at point: Point) {
        let color = RGB(selectedColor)
        let scale = max(1, min(4, Int(textSize)))
        let lines = (textValue.isEmpty ? "Text" : textValue).uppercased().components(separatedBy: .newlines)
        for (lineIndex, line) in lines.enumerated() {
            var cursorX = point.x
            let cursorY = point.y + lineIndex * 8 * scale
            for character in line {
                if character == " " {
                    cursorX += 3 * scale
                    continue
                }
                let glyph = pixelFont[character] ?? pixelFont["?"]!
                for (glyphY, row) in glyph.enumerated() {
                    for (glyphX, cell) in row.enumerated() where cell == "1" {
                        for sy in 0..<scale {
                            for sx in 0..<scale {
                                setPixel(Point(x: cursorX + glyphX * scale + sx, y: cursorY + glyphY * scale + sy), color)
                            }
                        }
                    }
                }
                cursorX += 6 * scale
            }
        }
    }

    private func importPhoto(_ item: PhotosPickerItem?) {
        guard let item else { return }
        Task {
            guard let data = try? await item.loadTransferable(type: Data.self),
                  let image = UIImage(data: data) else {
                status = "Image import failed"
                imagePickerStatus = "Import failed"
                return
            }
            guard let source = imagePixels(from: image) else {
                status = "Image import failed"
                imagePickerStatus = "Import failed"
                return
            }
            imageSourcePixels = source
            imagePosition = Point(x: 0, y: 0)
            imageSize = 64
            imageDragOffset = nil
            previewPixels = compositeImageLayer(source: source)
            status = "Drag image to move, resize, then place"
            imagePickerStatus = "Image ready to place"
        }
    }

    private func imagePixels(from image: UIImage) -> [RGB]? {
        let width = matrixSize
        let height = matrixSize
        let bytesPerPixel = 4
        let bytesPerRow = width * bytesPerPixel
        var rgba = [UInt8](repeating: 0, count: width * height * bytesPerPixel)
        guard let context = CGContext(
            data: &rgba,
            width: width,
            height: height,
            bitsPerComponent: 8,
            bytesPerRow: bytesPerRow,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ) else {
            return nil
        }

        context.setFillColor(UIColor.black.cgColor)
        context.fill(CGRect(x: 0, y: 0, width: width, height: height))
        UIGraphicsPushContext(context)
        let scale = min(CGFloat(matrixSize) / image.size.width, CGFloat(matrixSize) / image.size.height)
        let drawWidth = image.size.width * scale
        let drawHeight = image.size.height * scale
        image.draw(in: CGRect(
            x: (CGFloat(matrixSize) - drawWidth) / 2,
            y: (CGFloat(matrixSize) - drawHeight) / 2,
            width: drawWidth,
            height: drawHeight
        ))
        UIGraphicsPopContext()

        var next = Array(repeating: RGB.black, count: matrixSize * matrixSize)
        for y in 0..<matrixSize {
            for x in 0..<matrixSize {
                let sourceY = matrixSize - 1 - y
                let offset = sourceY * bytesPerRow + x * bytesPerPixel
                let red = rgba[offset]
                let green = rgba[offset + 1]
                let blue = rgba[offset + 2]
                next[pixelIndex(x, y)] = RGB(r: red, g: green, b: blue)
            }
        }
        return next
    }

    private func pointInImageLayer(_ point: Point) -> Bool {
        guard imageSourcePixels != nil else { return false }
        let size = Int(imageSize)
        return point.x >= imagePosition.x
            && point.x < imagePosition.x + size
            && point.y >= imagePosition.y
            && point.y < imagePosition.y + size
    }

    private func imageLayerPixels() -> [RGB] {
        guard let source = imageSourcePixels else { return pixels }
        return compositeImageLayer(source: source)
    }

    private func compositeImageLayer(source: [RGB]) -> [RGB] {
        var next = pixels
        let size = max(1, Int(imageSize))
        for y in 0..<size {
            for x in 0..<size {
                let tx = imagePosition.x + x
                let ty = imagePosition.y + y
                guard inBounds(tx, ty) else { continue }
                let sx = min(matrixSize - 1, Int((Double(x) / Double(size)) * Double(matrixSize)))
                let sy = min(matrixSize - 1, Int((Double(y) / Double(size)) * Double(matrixSize)))
                let color = source[pixelIndex(sx, sy)]
                if !color.isOff {
                    next[pixelIndex(tx, ty)] = color
                }
            }
        }
        return next
    }

    private func placeImageLayer() {
        guard imageSourcePixels != nil else { return }
        recordHistory()
        pixels = imageLayerPixels()
        imageSourcePixels = nil
        imageDragOffset = nil
        previewPixels = nil
        status = "Image placed"
        imagePickerStatus = "Image placed"
    }

    private func cancelImageLayer() {
        imageSourcePixels = nil
        imageDragOffset = nil
        previewPixels = nil
        status = "Image canceled"
        imagePickerStatus = "Import photo to 64x64"
    }

    private func applyIncomingPayload(_ payload: MatrixPayload) {
        let hex = payload.frameHex
        guard hex.count >= matrixSize * matrixSize * 6 else { return }
        var next = Array(repeating: RGB.black, count: matrixSize * matrixSize)
        var cursor = hex.startIndex
        for i in 0..<(matrixSize * matrixSize) {
            guard let chunkEnd = hex.index(cursor, offsetBy: 6, limitedBy: hex.endIndex),
                  let value = UInt32(hex[cursor..<chunkEnd], radix: 16) else { break }
            next[i] = RGB(
                r: UInt8((value >> 16) & 0xff),
                g: UInt8((value >> 8) & 0xff),
                b: UInt8(value & 0xff)
            )
            cursor = chunkEnd
        }
        previewPixels = nil
        imageSourcePixels = nil
        imageDragOffset = nil
        undoStack = []
        redoStack = []
        pixels = next
        if payload.brightness >= 1 && payload.brightness <= 100 {
            brightness = Double(payload.brightness)
        }
    }

    private func loadFromBackend() {
        let normalizedBackendURL = normalizedURLString(backendURL)
        backendURL = normalizedBackendURL
        guard let url = URL(string: normalizedBackendURL), url.host?.isEmpty == false else {
            return
        }
        var request = URLRequest(url: url)
        request.httpMethod = "GET"
        request.setValue("application/json, text/plain, */*", forHTTPHeaderField: "Accept")
        request.setValue("Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1", forHTTPHeaderField: "User-Agent")
        status = "Loading last frame..."
        backendSession.dataTask(with: request) { data, response, error in
            DispatchQueue.main.async {
                guard let data, error == nil,
                      let http = response as? HTTPURLResponse, (200...299).contains(http.statusCode) else {
                    status = "Ready"
                    return
                }
                guard let payload = try? JSONDecoder().decode(MatrixPayload.self, from: data),
                      payload.hasFrame else {
                    status = "No saved frame yet"
                    return
                }
                applyIncomingPayload(payload)
                status = "Loaded last frame"
            }
        }.resume()
    }

    private func sendToBackend() {
        let normalizedBackendURL = normalizedURLString(backendURL)
        backendURL = normalizedBackendURL
        guard let url = URL(string: normalizedBackendURL), let host = url.host, !host.isEmpty else {
            status = "Add backend URL in Settings"
            showingSettings = true
            return
        }
        var request = URLRequest(url: url)
        request.httpMethod = "PUT"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.setValue("application/json, text/plain, */*", forHTTPHeaderField: "Accept")
        request.setValue("Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1", forHTTPHeaderField: "User-Agent")
        let payload = MatrixPayload(
            ok: true,
            hasFrame: true,
            width: matrixSize,
            height: matrixSize,
            frameHex: frameHexString(),
            brightness: Int(max(1, min(100, brightness)).rounded()),
            sequence: Int((Date().timeIntervalSince1970 * 1000).truncatingRemainder(dividingBy: 2_147_483_647)),
            updatedAt: ISO8601DateFormatter().string(from: Date())
        )
        request.httpBody = try? JSONEncoder().encode(payload)
        status = "Sending to \(host)..."
        backendSession.dataTask(with: request) { _, response, error in
            DispatchQueue.main.async {
                if let error {
                    let nsError = error as NSError
                    if nsError.domain == NSURLErrorDomain && nsError.code == NSURLErrorCannotFindHost {
                        status = "Cannot resolve host: \(host)"
                    } else if nsError.domain == NSURLErrorDomain && [
                        NSURLErrorSecureConnectionFailed,
                        NSURLErrorServerCertificateUntrusted,
                        NSURLErrorServerCertificateHasBadDate,
                        NSURLErrorServerCertificateHasUnknownRoot,
                        NSURLErrorServerCertificateNotYetValid
                    ].contains(nsError.code) {
                        status = "TLS failed for \(host)"
                    } else if nsError.domain == NSURLErrorDomain && nsError.code == NSURLErrorTimedOut {
                        status = "Backend timed out. Check host."
                    } else if nsError.domain == NSURLErrorDomain && nsError.code == NSURLErrorCannotConnectToHost {
                        status = "Cannot connect to \(host)"
                    } else {
                        status = "Send failed: \(error.localizedDescription)"
                    }
                } else if let http = response as? HTTPURLResponse, !(200...299).contains(http.statusCode) {
                    status = "Backend returned \(http.statusCode)"
                } else {
                    status = "Sent to backend"
                }
            }
        }.resume()
    }

    private func frameHexString() -> String {
        imageLayerPixels().map { pixel in
            String(format: "%02x%02x%02x", pixel.r, pixel.g, pixel.b)
        }.joined()
    }

    private func normalizedURLString(_ value: String) -> String {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
            .replacingOccurrences(of: "cory-pearl.gt.lc", with: "cory-pearl.gt.tc")
        guard !trimmed.isEmpty else { return "" }
        if trimmed.hasPrefix("http://") || trimmed.hasPrefix("https://") {
            return trimmed
        }
        return "https://\(trimmed)"
    }
}

struct Point: Equatable {
    var x: Int
    var y: Int
}

struct SectionBox<Content: View>: View {
    let title: String
    @ViewBuilder var content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title.uppercased())
                .font(.caption)
                .fontWeight(.semibold)
                .foregroundStyle(.secondary)
            content
        }
        .padding(.top, 8)
    }
}

struct BrandMark: View {
    var body: some View {
        RoundedRectangle(cornerRadius: 8)
            .fill(
                AngularGradient(
                    colors: [
                        Color(red: 1.0, green: 0.19, blue: 0.35),
                        Color(red: 1.0, green: 0.74, blue: 0.18),
                        Color(red: 0.26, green: 0.82, blue: 0.54),
                        Color(red: 0.07, green: 0.40, blue: 1.0),
                        Color(red: 1.0, green: 0.19, blue: 0.35)
                    ],
                    center: .center,
                    startAngle: .degrees(30),
                    endAngle: .degrees(390)
                )
            )
            .overlay {
                GeometryReader { proxy in
                    let line = max(2, proxy.size.width * 0.08)
                    ZStack {
                        Rectangle()
                            .fill(.white.opacity(0.45))
                            .frame(width: line)
                        Rectangle()
                            .fill(.white.opacity(0.45))
                            .frame(height: line)
                    }
                    .frame(width: proxy.size.width, height: proxy.size.height)
                }
            }
            .frame(width: 38, height: 38)
    }
}

struct SettingsView: View {
    @Binding var backendURL: String
    @Binding var darkMode: Bool
    @Environment(\.dismiss) private var dismiss
    @FocusState private var urlFieldFocused: Bool

    var body: some View {
        NavigationStack {
            Form {
                Section("Backend") {
                    TextField(defaultBackendURL, text: $backendURL)
                        .keyboardType(.URL)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .submitLabel(.done)
                        .focused($urlFieldFocused)
                        .onSubmit {
                            backendURL = normalizedURLString(backendURL)
                            urlFieldFocused = false
                        }
                    Button("Reset to default URL") {
                        backendURL = defaultBackendURL
                        urlFieldFocused = false
                    }
                }
                Section("Appearance") {
                    Toggle("Dark mode", isOn: $darkMode)
                }
                Section {
                    Text("Send writes the latest 64x64 RGB frame to your configured backend.")
                        .foregroundStyle(.secondary)
                }
            }
            .navigationTitle("Settings")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Done") { dismiss() }
                }
                ToolbarItemGroup(placement: .keyboard) {
                    Spacer()
                    Button("Done") { urlFieldFocused = false }
                }
            }
        }
    }

    private func normalizedURLString(_ value: String) -> String {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
            .replacingOccurrences(of: "cory-pearl.gt.lc", with: "cory-pearl.gt.tc")
        guard !trimmed.isEmpty else { return "" }
        if trimmed.hasPrefix("http://") || trimmed.hasPrefix("https://") {
            return trimmed
        }
        return "https://\(trimmed)"
    }
}
