// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI
import UIKit

/// UIKit supplies one complete keyboard rectangle, including the accessory.
/// Disable separate SwiftUI avoidance so switching input views cannot leave a
/// stale inset or put the status row underneath the accessory.
struct WorkspaceKeyboardContainer:UIViewControllerRepresentable {
    let workspace:WorkspaceModel
    func makeUIViewController(context:Context)->WorkspaceKeyboardController { WorkspaceKeyboardController(workspace:workspace) }
    func updateUIViewController(_ controller:WorkspaceKeyboardController,context:Context) {}
}
final class WorkspaceKeyboardController:UIViewController {
    private let content:UIHostingController<WorkspaceView>
    private var bottom:NSLayoutConstraint?
    private var keyboardFrame:CGRect?
    private var observers:[NSObjectProtocol]=[]
    init(workspace:WorkspaceModel) {
        content=UIHostingController(rootView:WorkspaceView(workspace:workspace))
        super.init(nibName:nil,bundle:nil)
        content.safeAreaRegions = .container
    }
    required init?(coder:NSCoder) { fatalError("init(coder:) has not been implemented") }
    override func loadView() {
        view=UIView();view.backgroundColor = .black
        addChild(content)
        let child=content.view!
        child.translatesAutoresizingMaskIntoConstraints=false
        child.clipsToBounds=true
        view.addSubview(child)
        let edge=child.bottomAnchor.constraint(equalTo:view.safeAreaLayoutGuide.bottomAnchor)
        bottom=edge
        NSLayoutConstraint.activate([
            child.topAnchor.constraint(equalTo:view.safeAreaLayoutGuide.topAnchor),
            child.leadingAnchor.constraint(equalTo:view.safeAreaLayoutGuide.leadingAnchor),
            child.trailingAnchor.constraint(equalTo:view.safeAreaLayoutGuide.trailingAnchor),edge
        ])
        content.didMove(toParent:self)
        for name in [UIResponder.keyboardWillChangeFrameNotification,UIResponder.keyboardDidChangeFrameNotification] {
            observers.append(NotificationCenter.default.addObserver(forName:name,object:nil,queue:.main) { [weak self] notification in
                MainActor.assumeIsolated { self?.keyboardChanged(notification) }
            })
        }
    }
    deinit { for observer in observers { NotificationCenter.default.removeObserver(observer) } }
    override func viewDidLayoutSubviews() { super.viewDidLayoutSubviews();updateKeyboardInset() }
    private func keyboardChanged(_ notification:Notification) {
        guard let frame=notification.userInfo?[UIResponder.keyboardFrameEndUserInfoKey] as? CGRect else { return }
        keyboardFrame=frame
        updateKeyboardInset()
        let duration=notification.userInfo?[UIResponder.keyboardAnimationDurationUserInfoKey] as? Double ?? 0
        let curve=notification.userInfo?[UIResponder.keyboardAnimationCurveUserInfoKey] as? UInt ?? 0
        UIView.animate(withDuration:duration,delay:0,options:[UIView.AnimationOptions(rawValue:curve << 16),.beginFromCurrentState,.allowUserInteraction]) { self.view.layoutIfNeeded() }
    }
    private func updateKeyboardInset() {
        guard let bottom,let window=view.window else { return }
        var overlap:CGFloat=0
        if let keyboardFrame {
            let frame=view.convert(window.convert(keyboardFrame,from:window.screen.coordinateSpace),from:window)
            // Floating/undocked keyboards do not resize the whole workspace.
            if frame.maxY >= view.bounds.maxY-1,frame.width >= view.bounds.width*0.8 {
                overlap=max(0,view.bounds.maxY-frame.minY-view.safeAreaInsets.bottom)
            }
        }
        let value = -min(overlap,max(0,view.bounds.height-view.safeAreaInsets.top-view.safeAreaInsets.bottom-56))
        if bottom.constant != value { bottom.constant=value }
    }
}
