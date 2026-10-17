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
    var body: some View {
        if let session=workspace.session(for:item) {
            if case .terminal = item {
                SessionView(workspace:workspace,session:session)
            } else if case .wayland(_,let window)=item {
                WaylandContent(workspace:workspace,session:session,windowID:window,visible:workspace.isVisible(item))
            }
        }
    }
}
struct WorkspaceWindow: View {
    @ObservedObject var workspace: WorkspaceModel
    let item: WorkspaceItemID
    let viewport: CGSize
    let headerFrame: CGRect
    @State private var geometry=DisplayWindowGeometry()
    @State private var dragOrigin: CGPoint?
    @State private var resizeOrigin: CGSize?
    @State private var docking=false
    private var floating: Bool { workspace.layout.placement(item) == .floating }
    private var visible: Bool { workspace.isVisible(item) }
    var body: some View {
        VStack(spacing:0) {
            if floating {
            HStack(spacing:12) {
                if let session=workspace.session(for:item) {
                    WorkspaceTabLabel(session:session,item:item).lineLimit(1)
                        .accessibilityElement(children:.combine)
                        .accessibilityIdentifier("window-title-\(item.token)")
                }
                Spacer()
                Button { workspace.minimize(item) } label: { Image(systemName:"minus") }.accessibilityLabel("最小化到标签栏")
                    .accessibilityIdentifier("minimize-\(item.token)")
                Button { workspace.dock(item) } label: { Image(systemName:"arrow.up.left.and.arrow.down.right") }.accessibilityLabel("停靠为标签页")
                    .accessibilityIdentifier("dock-\(item.token)")
                Button { workspace.closeItem(item) } label: { Image(systemName:"xmark") }.accessibilityLabel("关闭窗口")
            }.font(.callout).padding(.horizontal,12).frame(height:40)
                .background(docking ? Color.mint.opacity(0.35) : Color(white:0.16))
                .contentShape(Rectangle())
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
                // Native controls are outside the clipped client viewport.
                .zIndex(2)
            }
            WorkspaceContent(workspace:workspace,item:item)
                .frame(maxWidth:.infinity,maxHeight:.infinity)
                .clipped()
            if floating {
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
            }
        }.frame(width:floating ? geometry.size.width : viewport.width,
                height:floating ? geometry.size.height : viewport.height)
            .background(Color.black)
            .clipShape(RoundedRectangle(cornerRadius:floating ? 10 : 0))
            .overlay(RoundedRectangle(cornerRadius:floating ? 10 : 0).stroke(floating ? (workspace.layout.focused == item ? Color.mint : Color.white.opacity(0.3)) : .clear))
            .shadow(radius:floating ? 12 : 0)
            .offset(x:floating ? geometry.origin.x : 0,y:floating ? geometry.origin.y : 0)
            .opacity(visible ? 1 : 0).allowsHitTesting(visible).accessibilityHidden(!visible)
            .onAppear { geometry=workspace.layout.frames[item] ?? DisplayWindowGeometry();geometry.constrain(to:viewport) }
            .onChange(of:viewport) { _,_ in geometry.constrain(to:viewport) }
            .onChange(of:workspace.layout.frames[item]) { _,value in
                if let value, value != geometry { geometry=value;geometry.constrain(to:viewport) }
            }
            // Container identifiers propagate to descendants in SwiftUI and
            // replace title/button identifiers. Identify concrete controls only.
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
