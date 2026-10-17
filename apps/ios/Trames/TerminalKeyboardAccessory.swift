// SPDX-License-Identifier: GPL-3.0-or-later
import UIKit
import SwiftTerm

/// UIKit hosts the original SwiftTerm accessory through this controller. Keep
/// TerminalView.inputAccessoryView unchanged: SwiftTerm reads Ctrl state there.
final class TerminalKeyboardAccessoryController: UIInputViewController {
    let accessory: TerminalAccessory
    init(accessory: TerminalAccessory) {
        self.accessory=accessory
        super.init(nibName:nil,bundle:nil)
    }
    required init?(coder:NSCoder) { fatalError("init(coder:) has not been implemented") }
    override func loadView() { inputView=TerminalKeyboardAccessoryContainer(accessory:accessory) }
}
final class TerminalKeyboardAccessoryContainer: UIInputView {
    let accessory: TerminalAccessory
    private let rowHeight:CGFloat
    /// Leave the Stage Manager/system window resize corner free of controls.
    static let trailingClearance:CGFloat=48
    init(accessory:TerminalAccessory) {
        self.accessory=accessory
        rowHeight=max(36,accessory.bounds.height)
        super.init(frame:CGRect(x:0,y:0,width:accessory.bounds.width,height:rowHeight),inputViewStyle:.keyboard)
        allowsSelfSizing=true
        accessory.allowsSelfSizing=false
        addSubview(accessory)
    }
    required init?(coder:NSCoder) { fatalError("init(coder:) has not been implemented") }
    override var intrinsicContentSize:CGSize { CGSize(width:UIView.noIntrinsicMetric,height:rowHeight) }
    override func layoutSubviews() {
        super.layoutSubviews()
        let rect=CGRect(x:safeAreaInsets.left,y:0,
            width:max(0,bounds.width-safeAreaInsets.left-safeAreaInsets.right-Self.trailingClearance),height:rowHeight)
        if accessory.frame != rect || !accessory.subviews.contains(where: { $0 is UIButton }) {
            accessory.frame=rect
            // A frame change inside a host view does not invoke SwiftTerm's
            // Swift bounds observer. Build its original buttons explicitly.
            let control=accessory.controlModifier
            accessory.setupUI()
            accessory.controlModifier=control
        }
        accessory.layoutIfNeeded()
        // SwiftTerm rebuilds its controls after width changes. Give the actual
        // toggle a stable public accessibility identity after each layout.
        for case let button as UIButton in accessory.subviews {
            if button.actions(forTarget:accessory,forControlEvent:.touchDown)?.contains("toggleInputKeyboard:") == true {
                button.accessibilityIdentifier="terminal-extended-keyboard-toggle"
                button.accessibilityLabel="扩展键盘"
            }
        }
    }
}
