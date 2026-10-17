// SPDX-License-Identifier: GPL-3.0-or-later
import UIKit
import SwiftTerm

/// Terminal columns fit the viewport; only scrollback needs a scrolling axis.
final class TerminalViewport: TerminalView {
    var onFocusRequested: (() -> Void)?
    private var wantsKeyboardFocus = false
    private var acceptsKeyboardFocus = true
    private var focusRevision: UInt64 = 0
    private var changingFocus = false
    func setKeyboardFocus(_ value: Bool, visible: Bool = true, deferred: Bool = false) {
        acceptsKeyboardFocus = visible
        wantsKeyboardFocus = value && visible
        focusRevision &+= 1
        if deferred { scheduleKeyboardFocus() }
        else { applyKeyboardFocus() }
    }
    private func scheduleKeyboardFocus() {
        let revision = focusRevision
        // UIViewRepresentable updates must not synchronously start a keyboard
        // transition: its safe-area resize can reenter the same SwiftUI update.
        DispatchQueue.main.async { [weak self] in
            guard let self, self.focusRevision == revision else { return }
            self.applyKeyboardFocus()
        }
    }
    private func applyKeyboardFocus() {
        guard !changingFocus else { return }
        if wantsKeyboardFocus && window != nil && !isFirstResponder { _ = becomeFirstResponder() }
        else if !wantsKeyboardFocus && isFirstResponder { _ = resignFirstResponder() }
    }
    override var canBecomeFirstResponder: Bool { acceptsKeyboardFocus && super.canBecomeFirstResponder }
    override func becomeFirstResponder() -> Bool {
        guard acceptsKeyboardFocus else { return false }
        // UIKit can reenter while updating the keyboard during a resize.
        guard !isFirstResponder, !changingFocus else { return isFirstResponder }
        changingFocus = true
        defer { changingFocus = false }
        let became = super.becomeFirstResponder()
        if became { onFocusRequested?() }
        return became
    }
    override func resignFirstResponder() -> Bool {
        guard isFirstResponder, !changingFocus else { return !isFirstResponder }
        changingFocus = true
        defer { changingFocus = false }
        return super.resignFirstResponder()
    }
    override func didMoveToWindow() {
        super.didMoveToWindow()
        if window != nil { scheduleKeyboardFocus() }
    }

    override init(frame: CGRect, font: UIFont?) {
        super.init(frame: frame, font: font)
        configureScrolling()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        configureScrolling()
    }

    private func configureScrolling() {
        contentInsetAdjustmentBehavior = .never
        alwaysBounceHorizontal = false
        showsHorizontalScrollIndicator = false
        isDirectionalLockEnabled = true
    }

    override var contentSize: CGSize {
        get { super.contentSize }
        set { super.contentSize = CGSize(width: max(0, bounds.width), height: newValue.height) }
    }

    override var contentOffset: CGPoint {
        get { super.contentOffset }
        set {
            super.contentOffset = CGPoint(x: 0, y: newValue.y)
            if newValue.x != 0 { setNeedsDisplay(bounds) }
        }
    }

    // UIKit can change the scroll view's bounds directly during a gesture or
    // keyboard adjustment, without calling the public contentOffset setter.
    override var bounds: CGRect {
        get { super.bounds }
        set {
            super.bounds = CGRect(origin: CGPoint(x: 0, y: newValue.origin.y), size: newValue.size)
            if newValue.origin.x != 0 { setNeedsDisplay(bounds) }
        }
    }

    override func setContentOffset(_ contentOffset: CGPoint, animated: Bool) {
        super.setContentOffset(CGPoint(x: 0, y: contentOffset.y), animated: animated)
        if contentOffset.x != 0 { setNeedsDisplay(bounds) }
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        // A fractional resize can leave the column count unchanged. Keep the
        // scrolling width in sync even when SwiftTerm does not resize its grid.
        if contentSize.width != bounds.width {
            contentSize = CGSize(width: bounds.width, height: contentSize.height)
        }
    }
}
