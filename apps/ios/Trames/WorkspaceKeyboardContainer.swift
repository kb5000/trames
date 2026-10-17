// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI
import UIKit

/// One UIKit keyboard layout guide owns the workspace's lower edge. SwiftUI's
/// separate keyboard avoidance must not retain an inset after reloadInputViews.
struct WorkspaceKeyboardContainer:UIViewControllerRepresentable {
    let workspace:WorkspaceModel
    func makeUIViewController(context:Context)->WorkspaceKeyboardController { WorkspaceKeyboardController(workspace:workspace) }
    func updateUIViewController(_ controller:WorkspaceKeyboardController,context:Context) {}
}
final class WorkspaceKeyboardController:UIViewController {
    private let content:UIHostingController<WorkspaceView>
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
        let keyboard=view.keyboardLayoutGuide
        keyboard.followsUndockedKeyboard=false
        keyboard.usesBottomSafeArea=true
        NSLayoutConstraint.activate([
            child.topAnchor.constraint(equalTo:view.safeAreaLayoutGuide.topAnchor),
            child.leadingAnchor.constraint(equalTo:view.safeAreaLayoutGuide.leadingAnchor),
            child.trailingAnchor.constraint(equalTo:view.safeAreaLayoutGuide.trailingAnchor),
            child.bottomAnchor.constraint(equalTo:keyboard.topAnchor)
        ])
        content.didMove(toParent:self)
    }
}
