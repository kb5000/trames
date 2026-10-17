// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI
import TramesKit

struct WaylandCanvasPane: UIViewRepresentable {
    @ObservedObject var workspace: WorkspaceModel
    @ObservedObject var session: TerminalSession
    let window: LocalWaylandWindow
    let visible: Bool
    private var item: WorkspaceItemID { .wayland(session.id,window.id) }
    func makeUIView(context: Context) -> UIView {
        do {
            let canvas=try WaylandCanvas()
            canvas.onError={ [weak session] in session?.state=$0 }
            return canvas
        } catch {
            let label=UILabel();label.text=error.localizedDescription;label.textColor = .white;return label
        }
    }
    func updateUIView(_ view: UIView, context: Context) {
        guard let canvas=view as? WaylandCanvas else { return }
        canvas.sceneOrigin = .zero;canvas.session=session.local;canvas.windowID=window.id
        canvas.presentationEnabled=visible
        canvas.sendInput={ [weak session] in session?.waylandCommand($0) }
        // Pointer/button events choose the actual child/popup surface.
        let id=item
        canvas.onFocus={ [weak workspace] in workspace?.focus(id,protocolFocus:false) }
        canvas.textContext=session.waylandText?.window == window.id ? session.waylandText : nil
        canvas.setKeyboardFocus(visible && workspace.layout.focused == item)
    }
}
private struct WaylandResizeRequest: Hashable {
    let width: Int
    let height: Int
    let mode: String
    let visible: Bool
    init(size: CGSize, mode: String, visible: Bool) {
        width=max(1,min(4096,Int(size.width)))
        height=max(1,min(4096,Int(size.height)))
        self.mode=mode;self.visible=visible
    }
}
/// The host content rectangle owns the logical viewport. Old client buffers
/// never enlarge it while a configure/ack/commit round trip is outstanding.
struct WaylandContent: View {
    @ObservedObject var workspace: WorkspaceModel
    @ObservedObject var session: TerminalSession
    let windowID: UInt64
    let visible: Bool
    var body: some View {
        if let window=session.waylandWindows.first(where:{ $0.id == windowID }) {
        GeometryReader { proxy in
            let request=WaylandResizeRequest(size:proxy.size,mode:window.mode,visible:visible)
            WaylandCanvasPane(workspace:workspace,session:session,window:window,visible:visible)
                .frame(width:proxy.size.width,height:proxy.size.height)
                .clipped()
                .task(id:request) {
                    guard request.visible else { return }
                    do { try await Task.sleep(for:.milliseconds(40)) } catch { return }
                    guard !Task.isCancelled else { return }
                    // Resend after a mode change even if the previous buffer
                    // already has this size: mode configure can still be queued.
                    session.waylandCommand(["op":"resize","window":window.id,
                        "width":request.width,"height":request.height])
                }
        }.clipped()
        }
    }
}
