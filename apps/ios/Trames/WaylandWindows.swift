// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI
import TramesKit

struct WaylandCanvasPane: UIViewRepresentable {
    @ObservedObject var workspace: WorkspaceModel
    @ObservedObject var session: TerminalSession
    let window: LocalWaylandWindow
    let sceneOrigin: CGPoint
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
        canvas.sceneOrigin=sceneOrigin;canvas.session=session.local;canvas.windowID=window.id
        canvas.sendInput={ [weak session] in session?.waylandCommand($0) }
        // Pointer/button events choose the actual child/popup surface. Sending
        // root focus here would dismiss the popup being clicked.
        let id=item
        canvas.onFocus={ [weak workspace] in workspace?.focus(id,protocolFocus:false) }
        canvas.textContext=session.waylandText?.window == window.id ? session.waylandText : nil
        canvas.setKeyboardFocus(workspace.layout.focused == item)
    }
}
/// Logical resolution follows the actual content area, with no internal title
/// bar in a docked tab. Popup scene origins stay in surface coordinates.
struct WaylandContent: View {
    @ObservedObject var workspace: WorkspaceModel
    @ObservedObject var session: TerminalSession
    let window: LocalWaylandWindow
    let contentOrigin: CGPoint
    let viewport: CGSize
    @State private var resizeTask: Task<Void,Never>?
    var body: some View {
        GeometryReader { proxy in
            let b=window.bounds
            let scene=b.count == 4 ? CGRect(x:CGFloat(b[0]),y:CGFloat(b[1]),width:CGFloat(b[2]),height:CGFloat(b[3])) : CGRect(origin:.zero,size:proxy.size)
            let visible=scene.intersection(CGRect(x:-contentOrigin.x,y:-contentOrigin.y,width:viewport.width,height:viewport.height))
            if !visible.isNull && !visible.isEmpty {
                WaylandCanvasPane(workspace:workspace,session:session,window:window,sceneOrigin:visible.origin)
                    .frame(width:visible.width,height:visible.height).offset(x:visible.minX,y:visible.minY)
            }
            Color.clear.frame(width:0,height:0)
                .onAppear { resize(proxy.size) }
                .onChange(of:proxy.size) { _, value in resize(value) }
        }.onDisappear { resizeTask?.cancel() }
    }
    private func resize(_ size: CGSize) {
        resizeTask?.cancel()
        let width=max(1,min(4096,Int(size.width))),height=max(1,min(4096,Int(size.height)))
        guard window.width != width || window.height != height else { return }
        resizeTask=Task { @MainActor in
            do { try await Task.sleep(for:.milliseconds(80)) } catch { return }
            session.waylandCommand(["op":"resize","window":window.id,"width":width,"height":height])
        }
    }
}
