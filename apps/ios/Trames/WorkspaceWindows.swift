// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI

struct WorkspaceTabLabel: View {
    @ObservedObject var session: TerminalSession
    let item: WorkspaceItemID
    var body: some View {
        switch item {
        case .terminal: Label(session.title,systemImage:"terminal")
        case .wayland(_,let id):
            let window=session.waylandWindows.first { $0.id == id }
            let title=window?.title ?? ""
            Label(title.isEmpty ? (window?.app_id.isEmpty == false ? window!.app_id : "图形应用") : title,systemImage:"window.ceiling")
        }
    }
}
struct WorkspaceContent: View {
    @ObservedObject var workspace: WorkspaceModel
    let item: WorkspaceItemID
    let origin: CGPoint
    let viewport: CGSize
    var body: some View {
        if let session=workspace.session(for:item) {
            if case .terminal = item {
                SessionView(workspace:workspace,session:session)
            } else if let window=workspace.window(for:item) {
                WaylandContent(workspace:workspace,session:session,window:window,contentOrigin:origin,viewport:viewport)
            }
        }
    }
}
struct WorkspaceFloatingWindow: View {
    @ObservedObject var workspace: WorkspaceModel
    let item: WorkspaceItemID
    let viewport: CGSize
    let headerFrame: CGRect
    @State private var geometry=DisplayWindowGeometry()
    @State private var dragOrigin: CGPoint?
    @State private var resizeOrigin: CGSize?
    @State private var docking=false
    var body: some View {
        VStack(spacing:0) {
            HStack(spacing:12) {
                if let session=workspace.session(for:item) { WorkspaceTabLabel(session:session,item:item).lineLimit(1) }
                Spacer()
                Button { workspace.minimize(item) } label: { Image(systemName:"minus") }.accessibilityLabel("最小化")
                Button { workspace.dock(item) } label: { Image(systemName:"arrow.up.left.and.arrow.down.right") }.accessibilityLabel("停靠为标签页")
                    .accessibilityIdentifier("dock-\(item.token)")
                Button { workspace.closeItem(item) } label: { Image(systemName:"xmark") }.accessibilityLabel("关闭窗口")
            }.font(.callout).padding(.horizontal,12).frame(height:40)
                .background(docking ? Color.mint.opacity(0.35) : Color(white:0.16))
                .contentShape(Rectangle()).accessibilityIdentifier("window-title-\(item.token)")
                .onTapGesture { workspace.focus(item) }
                .gesture(DragGesture(coordinateSpace:.named("workspace")).onChanged { value in
                    if dragOrigin == nil { dragOrigin=geometry.origin;workspace.focus(item) }
                    geometry.origin=CGPoint(x:dragOrigin!.x+value.translation.width,y:dragOrigin!.y+value.translation.height)
                    geometry.constrain(to:viewport)
                    docking=headerFrame.contains(value.location)
                }.onEnded { value in
                    dragOrigin=nil
                    workspace.saveFrame(item,geometry)
                    if headerFrame.contains(value.location) { workspace.dock(item) }
                    docking=false
                })
            WorkspaceContent(workspace:workspace,item:item,origin:CGPoint(x:geometry.origin.x,y:geometry.origin.y+40),viewport:viewport)
                .frame(maxWidth:.infinity,maxHeight:.infinity)
            HStack {
                Spacer()
                Image(systemName:"arrow.down.right.and.arrow.up.left").frame(width:44,height:28).contentShape(Rectangle())
                    .accessibilityIdentifier("window-resize-\(item.token)")
                    .gesture(DragGesture(coordinateSpace:.named("workspace")).onChanged { value in
                        if resizeOrigin == nil { resizeOrigin=geometry.size }
                        geometry.size=CGSize(width:resizeOrigin!.width+value.translation.width,height:resizeOrigin!.height+value.translation.height)
                        geometry.constrain(to:viewport)
                    }.onEnded { _ in resizeOrigin=nil;workspace.saveFrame(item,geometry) })
            }.background(Color(white:0.12))
        }.frame(width:geometry.size.width,height:geometry.size.height)
            .background(Color.black.clipShape(RoundedRectangle(cornerRadius:10)))
            .overlay(RoundedRectangle(cornerRadius:10).stroke(workspace.layout.focused == item ? Color.mint : Color.white.opacity(0.3)))
            .shadow(radius:12).offset(x:geometry.origin.x,y:geometry.origin.y)
            .onAppear { geometry=workspace.layout.frames[item] ?? DisplayWindowGeometry();geometry.constrain(to:viewport) }
            .onChange(of:viewport) { _,_ in geometry.constrain(to:viewport) }
            .accessibilityIdentifier("floating-\(item.token)")
    }
}
struct WorkspaceFramesKey: PreferenceKey {
    static var defaultValue: [String:CGRect] { [:] }
    static func reduce(value: inout [String:CGRect], nextValue: () -> [String:CGRect]) { value.merge(nextValue(),uniquingKeysWith: { _,new in new }) }
}
extension View {
    func workspaceFrame(_ key: String) -> some View {
        background(GeometryReader { proxy in Color.clear.preference(key:WorkspaceFramesKey.self,value:[key:proxy.frame(in:.named("workspace"))]) })
    }
}
