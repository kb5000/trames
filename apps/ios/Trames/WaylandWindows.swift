// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI
import TramesKit

struct WaylandCanvasPane: UIViewRepresentable {
    @ObservedObject var session: TerminalSession
    let window: LocalWaylandWindow
    let sceneOrigin: CGPoint
    func makeUIView(context: Context) -> UIView {
        do {
            let canvas = try WaylandCanvas()
            canvas.onError = { [weak session] message in session?.state = message }
            return canvas
        } catch {
            let label = UILabel(); label.text = error.localizedDescription; label.textColor = .white
            return label
        }
    }
    func updateUIView(_ view: UIView, context: Context) {
        guard let canvas = view as? WaylandCanvas else { return }
        canvas.sceneOrigin=sceneOrigin
        canvas.session = session.local; canvas.windowID = window.id; canvas.revision = window.revision
        canvas.sendInput = { [weak session] command in session?.waylandCommand(command) }
        canvas.onFocus = { [weak session] in session?.waylandFocus = window.id }
        canvas.textContext = session.waylandText?.window == window.id ? session.waylandText : nil
        if session.waylandFocus == window.id && !canvas.isFirstResponder {
            _ = canvas.becomeFirstResponder()
        } else if session.waylandFocus != window.id && canvas.isFirstResponder {
            canvas.resignFirstResponder()
        }
    }
}
struct WaylandWorkspace: View {
    @ObservedObject var session: TerminalSession
    let viewport: CGSize
    var body: some View {
        ZStack(alignment: .topLeading) {
            ForEach(session.waylandWindows) { window in
                if window.mode != "minimized" && ((window.mode == "floating" && session.waylandSelected == nil) || session.waylandSelected == window.id) {
                    WaylandWindow(session: session, window: window, viewport: viewport)
                        .zIndex(session.waylandFocus == window.id ? 1000 : Double(window.id % 999))
                }
            }
        }.frame(width: viewport.width,height: viewport.height,alignment: .topLeading)
    }
}
struct WaylandWindow: View {
    @ObservedObject var session: TerminalSession
    let window: LocalWaylandWindow
    let viewport: CGSize
    @State private var geometry = DisplayWindowGeometry()
    @State private var dragOrigin: CGPoint?
    @State private var resizeOrigin: CGSize?
    @State private var resizeTask: Task<Void,Never>?
    private var maximized: Bool { window.mode == "maximized" }
    private var size: CGSize { maximized ? viewport : geometry.size }
    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 12) {
                Text(window.title.isEmpty ? window.app_id : window.title).lineLimit(1)
                Spacer()
                Button { session.waylandMode(window.id,"minimized") } label: { Image(systemName:"minus") }.accessibilityLabel("最小化")
                Button { session.waylandMode(window.id,maximized ? "floating" : "maximized") } label: { Image(systemName:maximized ? "rectangle.on.rectangle" : "arrow.up.left.and.arrow.down.right") }.accessibilityLabel(maximized ? "还原" : "最大化为标签页")
                Button { session.waylandCommand(["op":"close","window":window.id]) } label: { Image(systemName:"xmark") }.accessibilityLabel("关闭窗口")
            }.font(.callout).padding(.horizontal,12).frame(height:40).background(Color(white:0.16))
                .contentShape(Rectangle())
                .onTapGesture {
                    session.waylandFocus = window.id
                    session.waylandCommand(["op":"focus","window":window.id])
                }
                .gesture(DragGesture(coordinateSpace:.global).onChanged { value in
                    guard !maximized else { return }
                    session.waylandFocus = window.id
                    if dragOrigin == nil { session.waylandCommand(["op":"focus","window":window.id]) }
                    if dragOrigin == nil { dragOrigin = geometry.origin }
                    geometry.origin = CGPoint(x:dragOrigin!.x + value.translation.width,y:dragOrigin!.y + value.translation.height)
                    geometry.constrain(to:viewport)
                }.onEnded { _ in dragOrigin = nil })
            GeometryReader { proxy in
                let bounds=window.bounds
                let scene=bounds.count==4 ? CGRect(x:CGFloat(bounds[0]),y:CGFloat(bounds[1]),width:CGFloat(bounds[2]),height:CGFloat(bounds[3])) : CGRect(origin:.zero,size:proxy.size)
                let origin=maximized ? CGPoint(x:0,y:40) : CGPoint(x:geometry.origin.x,y:geometry.origin.y+40)
                let visible=scene.intersection(CGRect(x:-origin.x,y:-origin.y,width:viewport.width,height:viewport.height))
                if !visible.isNull && !visible.isEmpty {
                    WaylandCanvasPane(session:session,window:window,sceneOrigin:visible.origin)
                        .frame(width:visible.width,height:visible.height)
                        .offset(x:visible.minX,y:visible.minY)
                }
            }.frame(maxWidth:.infinity,maxHeight:.infinity)
            if !maximized {
                HStack {
                    Text("\(window.width) × \(window.height)").font(.caption).foregroundStyle(.secondary)
                    Spacer()
                    Image(systemName:"arrow.down.right.and.arrow.up.left").frame(width:44,height:28)
                        .contentShape(Rectangle())
                        .gesture(DragGesture(coordinateSpace:.global).onChanged { value in
                            if resizeOrigin == nil { resizeOrigin = geometry.size }
                            geometry.size = CGSize(width:resizeOrigin!.width + value.translation.width,height:resizeOrigin!.height + value.translation.height)
                            geometry.constrain(to:viewport); requestSize()
                        }.onEnded { _ in resizeOrigin = nil; requestSize() })
                }.padding(.leading,12).background(Color(white:0.12))
            }
        }.frame(width:max(1,size.width),height:max(1,size.height))
            .background(Color.black.clipShape(RoundedRectangle(cornerRadius:maximized ? 0 : 10)))
            .overlay(RoundedRectangle(cornerRadius:maximized ? 0 : 10).stroke(Color.white.opacity(0.3)))
            .shadow(radius:maximized ? 0 : 12)
            .offset(x:maximized ? 0 : geometry.origin.x,y:maximized ? 0 : geometry.origin.y)
            .onAppear {
                if let saved = session.waylandGeometry[window.id] { geometry = saved }
                else {
                    geometry.size = CGSize(width:CGFloat(window.width),height:CGFloat(window.height)+68)
                    geometry.origin = CGPoint(x:CGFloat(24 + Int(window.id % 5)*24),y:CGFloat(60 + Int(window.id % 5)*24))
                }
                geometry.constrain(to:viewport); requestSize()
            }
            .onChange(of:viewport) { _,_ in geometry.constrain(to:viewport); requestSize() }
            .onChange(of:maximized) { _,_ in requestSize() }
            .onDisappear { resizeTask?.cancel(); session.waylandGeometry[window.id] = geometry }
    }
    private func requestSize() {
        resizeTask?.cancel()
        let width = max(1,min(4096,Int(size.width)))
        let height = max(1,min(4096,Int(size.height) - (maximized ? 40 : 68)))
        resizeTask = Task { @MainActor in
            do { try await Task.sleep(for:.milliseconds(80)) } catch { return }
            session.waylandCommand(["op":"resize","window":window.id,"width":width,"height":height])
        }
    }
}
