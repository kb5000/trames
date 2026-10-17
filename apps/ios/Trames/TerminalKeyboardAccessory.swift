// SPDX-License-Identifier: GPL-3.0-or-later
import UIKit
import SwiftTerm

/// Vend one accessory view to UIKit. The original upstream row owns its key
/// layout/actions; the surrounding view reserves the system resize corner.
final class TerminalKeyboardAccessory:UIInputView {
    let accessory:TerminalAccessory
    private weak var terminal:TerminalView?
    private let rowHeight:CGFloat
    private var resetObserver:NSObjectProtocol?
    static let trailingClearance:CGFloat=48
    init(accessory:TerminalAccessory,terminal:TerminalView) {
        self.accessory=accessory;self.terminal=terminal
        rowHeight=max(36,accessory.bounds.height)
        super.init(frame:CGRect(x:0,y:0,width:accessory.bounds.width,height:rowHeight),inputViewStyle:.keyboard)
        allowsSelfSizing=true;accessory.allowsSelfSizing=false
        accessibilityIdentifier="terminal-accessory"
        addSubview(accessory)
        // SwiftTerm casts inputAccessoryView to TerminalAccessory. A wrapper
        // instead bridges its original sticky Ctrl to the public modifier API.
        resetObserver=NotificationCenter.default.addObserver(forName:.terminalViewControlModifierReset,object:terminal,queue:.main) { [weak self] _ in
            MainActor.assumeIsolated {self?.accessory.controlModifier=false}
        }
    }
    required init?(coder:NSCoder) {fatalError("init(coder:) has not been implemented")}
    deinit {if let resetObserver {NotificationCenter.default.removeObserver(resetObserver)}}
    override var intrinsicContentSize:CGSize {CGSize(width:UIView.noIntrinsicMetric,height:rowHeight)}
    override func layoutSubviews() {
        super.layoutSubviews()
        let rect=CGRect(x:safeAreaInsets.left,y:0,width:max(0,bounds.width-safeAreaInsets.left-safeAreaInsets.right-Self.trailingClearance),height:rowHeight)
        if accessory.frame != rect || !accessory.subviews.contains(where:{$0 is UIButton}) {
            let control=accessory.controlModifier
            accessory.frame=rect;accessory.setupUI();accessory.controlModifier=control
        }
        accessory.layoutIfNeeded()
        for case let button as UIButton in accessory.subviews {
            let actions=button.actions(forTarget:accessory,forControlEvent:.touchDown) ?? []
            if actions.contains("toggleInputKeyboard:") {
                button.accessibilityIdentifier="terminal-extended-keyboard-toggle"
                button.accessibilityLabel="扩展键盘"
            }
            if actions.contains("ctrl:") {
                // Upstream ctrl: only toggles controlModifier. Forward that
                // same operation to the public fallback modifier as well.
                button.removeTarget(accessory,action:NSSelectorFromString("ctrl:"),for:.touchDown)
                button.addTarget(self,action:#selector(toggleControl),for:.touchDown)
                button.accessibilityIdentifier="terminal-control-modifier"
            }
        }
    }
    @objc private func toggleControl() {
        accessory.controlModifier.toggle()
        terminal?.controlModifier=accessory.controlModifier
    }
    override func didMoveToWindow() {
        super.didMoveToWindow()
        if window == nil {
            // Preserve upstream autorepeat cancellation when the wrapped row
            // leaves UIKit's keyboard hierarchy during dismissal or teardown.
            for case let button as UIButton in accessory.subviews {button.sendActions(for:.touchCancel)}
        }
    }
}
