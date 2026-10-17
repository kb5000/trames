// SPDX-License-Identifier: GPL-3.0-or-later
import UIKit
import SwiftTerm

/// Present the original accessory as the controller's root, preserving the
/// TerminalView cast used for Ctrl and all upstream targets/actions. Nesting a
/// second UIInputView inside an accessory controller leaves an empty row when
/// UIKit attaches the input views, even though standalone layout succeeds.
final class TerminalKeyboardAccessoryController: UIInputViewController {
    let accessory:TerminalAccessory
    static let trailingClearance:CGFloat=48
    private var naturalWidths:[ObjectIdentifier:CGFloat]=[:]
    private var laidOutSize=CGSize.zero
    init(accessory:TerminalAccessory) {
        self.accessory=accessory
        super.init(nibName:nil,bundle:nil)
    }
    required init?(coder:NSCoder) { fatalError("init(coder:) has not been implemented") }
    override func loadView() {
        inputView=accessory
        accessory.accessibilityIdentifier="terminal-accessory"
    }
    override func viewWillLayoutSubviews() {
        super.viewWillLayoutSubviews()
        if laidOutSize != accessory.bounds.size || !accessory.subviews.contains(where:{$0 is UIButton}) {
            let control=accessory.controlModifier
            accessory.setupUI()
            accessory.controlModifier=control
            naturalWidths.removeAll()
            laidOutSize=accessory.bounds.size
        } else {
            // The upstream layout uses each button's current width. Restore its
            // natural width first so repeated layouts never shrink keys again.
            for case let button as UIButton in accessory.subviews {
                if let width=naturalWidths[ObjectIdentifier(button)] {button.frame.size.width=width}
            }
        }
        accessory.layoutSubviews()
    }
    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        let width=accessory.bounds.width
        guard width>0 else {return}
        let left=accessory.safeAreaInsets.left
        let available=max(0,width-left-accessory.safeAreaInsets.right-Self.trailingClearance)
        let scale=available/width
        var natural:[ObjectIdentifier:CGFloat]=[:]
        for case let button as UIButton in accessory.subviews {
            var frame=button.frame
            natural[ObjectIdentifier(button)]=frame.width
            frame.origin.x=left+frame.origin.x*scale
            frame.size.width *= scale
            button.frame=frame
            if button.actions(forTarget:accessory,forControlEvent:.touchDown)?.contains("toggleInputKeyboard:") == true {
                button.accessibilityIdentifier="terminal-extended-keyboard-toggle"
                button.accessibilityLabel="扩展键盘"
            }
        }
        naturalWidths=natural
    }
}
