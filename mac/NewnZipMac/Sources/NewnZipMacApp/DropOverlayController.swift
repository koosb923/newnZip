import AppKit

@MainActor
final class DropOverlayController {
    private let overlayWidth: CGFloat = 56
    private let window: NSPanel
    private let dropView: DropOverlayView
    private var dragMonitor: Any?
    private var mouseUpMonitor: Any?
    private var isShowingForCurrentDrag = false
    private var activeDragPasteboardChangeCount: Int?
    private var lastPresentedFileDragChangeCount: Int?

    init(onDrop: @escaping @MainActor ([URL]) -> Void) {
        dropView = DropOverlayView(onDrop: onDrop)
        window = NSPanel(
            contentRect: NSRect(x: 0, y: 0, width: overlayWidth, height: 420),
            styleMask: [.borderless, .nonactivatingPanel],
            backing: .buffered,
            defer: false
        )
        window.contentView = dropView
        window.isOpaque = false
        window.backgroundColor = .clear
        window.alphaValue = 0.9
        window.hasShadow = true
        window.hidesOnDeactivate = false
        window.level = .statusBar
        window.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        _ = positionWindow()
    }

    func start() {
        guard dragMonitor == nil else {
            return
        }

        dragMonitor = NSEvent.addGlobalMonitorForEvents(matching: [.leftMouseDragged]) { [weak self] event in
            Task { @MainActor in
                self?.handleDrag(event: event)
            }
        }
        mouseUpMonitor = NSEvent.addGlobalMonitorForEvents(matching: [.leftMouseUp]) { [weak self] _ in
            Task { @MainActor in
                self?.hideSoon()
            }
        }
    }

    func stop() {
        if let dragMonitor {
            NSEvent.removeMonitor(dragMonitor)
            self.dragMonitor = nil
        }
        if let mouseUpMonitor {
            NSEvent.removeMonitor(mouseUpMonitor)
            self.mouseUpMonitor = nil
        }
        isShowingForCurrentDrag = false
        activeDragPasteboardChangeCount = nil
        window.orderOut(nil)
    }

    private func handleDrag(event: NSEvent) {
        let dragPasteboard = NSPasteboard(name: .drag)
        let changeCount = dragPasteboard.changeCount

        if isShowingForCurrentDrag, activeDragPasteboardChangeCount == changeCount {
            return
        }

        let urls = fileURLs(from: dragPasteboard)
        guard !urls.isEmpty else {
            if isShowingForCurrentDrag {
                activeDragPasteboardChangeCount = nil
            }
            window.orderOut(nil)
            return
        }

        guard lastPresentedFileDragChangeCount != changeCount else {
            return
        }

        guard positionWindow(mouseLocation: NSEvent.mouseLocation) else {
            window.orderOut(nil)
            return
        }
        isShowingForCurrentDrag = true
        activeDragPasteboardChangeCount = changeCount
        lastPresentedFileDragChangeCount = changeCount
        window.orderFrontRegardless()
    }

    private func hideSoon() {
        Task {
            try? await Task.sleep(for: .milliseconds(250))
            isShowingForCurrentDrag = false
            activeDragPasteboardChangeCount = nil
            window.orderOut(nil)
        }
    }

    private func positionWindow(mouseLocation: CGPoint? = nil) -> Bool {
        let screen = screen(containing: mouseLocation) ?? NSScreen.main ?? NSScreen.screens.first
        guard let visibleFrame = screen?.visibleFrame else {
            return false
        }
        let dockSide = AppSettings.shared.dragOverlayDockSide
        let fallback = CGPoint(x: visibleFrame.midX, y: visibleFrame.midY)
        let mouse = mouseLocation ?? fallback
        if mouse.y > visibleFrame.maxY - 90 {
            return false
        }

        let targetX = dockSide == .left ? visibleFrame.minX : visibleFrame.maxX - overlayWidth
        let targetFrame = NSRect(
            x: targetX,
            y: visibleFrame.minY,
            width: overlayWidth,
            height: visibleFrame.height
        )
        window.setFrame(targetFrame, display: false)
        dropView.applyDockSide(dockSide)
        return true
    }

    private func screen(containing point: CGPoint?) -> NSScreen? {
        guard let point else {
            return nil
        }
        return NSScreen.screens.first { screen in
            screen.frame.contains(point)
        }
    }

    private func fileURLs(from pasteboard: NSPasteboard) -> [URL] {
        let objects = pasteboard.readObjects(
            forClasses: [NSURL.self],
            options: [.urlReadingFileURLsOnly: true]
        ) ?? []
        return objects.compactMap { object in
            if let url = object as? URL {
                return url
            }
            return (object as? NSURL) as URL?
        }
    }
}

@MainActor
private final class DropOverlayView: NSVisualEffectView {
    private let onDrop: @MainActor ([URL]) -> Void
    private let titleLabel = NSTextField(labelWithString: "newnZip")
    private let detailLabel = NSTextField(labelWithString: "놓아서\n압축/해제")
    private let cornerRadius: CGFloat = 18

    init(onDrop: @escaping @MainActor ([URL]) -> Void) {
        self.onDrop = onDrop
        super.init(frame: .zero)
        material = .hudWindow
        blendingMode = .behindWindow
        state = .active
        wantsLayer = true
        layer?.masksToBounds = true
        layer?.borderWidth = 1
        layer?.borderColor = NSColor.separatorColor.withAlphaComponent(0.35).cgColor
        registerForDraggedTypes([.fileURL])
        setupLabels()
        applyDockSide(AppSettings.shared.dragOverlayDockSide)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func draggingEntered(_ sender: any NSDraggingInfo) -> NSDragOperation {
        fileURLs(from: sender.draggingPasteboard).isEmpty ? [] : .copy
    }

    override func performDragOperation(_ sender: any NSDraggingInfo) -> Bool {
        let urls = fileURLs(from: sender.draggingPasteboard)
        guard !urls.isEmpty else {
            return false
        }
        onDrop(urls)
        return true
    }

    func applyDockSide(_ dockSide: DragOverlayDockSide) {
        guard let layer else {
            return
        }
        layer.cornerRadius = cornerRadius
        layer.maskedCorners = dockSide == .left
            ? [.layerMaxXMinYCorner, .layerMaxXMaxYCorner]
            : [.layerMinXMinYCorner, .layerMinXMaxYCorner]
    }

    private func setupLabels() {
        titleLabel.font = .systemFont(ofSize: 13, weight: .semibold)
        titleLabel.textColor = .labelColor
        titleLabel.alignment = .center

        detailLabel.font = .systemFont(ofSize: 11, weight: .regular)
        detailLabel.textColor = .secondaryLabelColor
        detailLabel.alignment = .center
        detailLabel.maximumNumberOfLines = 2

        let stack = NSStackView(views: [titleLabel, detailLabel])
        stack.orientation = .vertical
        stack.alignment = .centerX
        stack.spacing = 6
        stack.translatesAutoresizingMaskIntoConstraints = false
        addSubview(stack)

        NSLayoutConstraint.activate([
            stack.centerXAnchor.constraint(equalTo: centerXAnchor),
            stack.centerYAnchor.constraint(equalTo: centerYAnchor),
            stack.leadingAnchor.constraint(greaterThanOrEqualTo: leadingAnchor, constant: 8),
            stack.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -8)
        ])
    }

    private func fileURLs(from pasteboard: NSPasteboard) -> [URL] {
        let objects = pasteboard.readObjects(
            forClasses: [NSURL.self],
            options: [.urlReadingFileURLsOnly: true]
        ) ?? []
        return objects.compactMap { object in
            if let url = object as? URL {
                return url
            }
            return (object as? NSURL) as URL?
        }
    }
}
