// SPDX-License-Identifier: GPL-3.0-or-later
import UIKit
import TramesKit

private final class WaylandTextPosition: UITextPosition {
    let offset: Int
    init(_ offset: Int) { self.offset = offset }
}
private final class WaylandTextRange: UITextRange {
    let value: NSRange
    init(_ value: NSRange) { self.value = value }
    override var start: UITextPosition { WaylandTextPosition(value.location) }
    override var end: UITextPosition { WaylandTextPosition(NSMaxRange(value)) }
    override var isEmpty: Bool { value.length == 0 }
}
/// UIKit owns composition/candidates. This document is a local mirror of the
/// focused Wayland text-input context, with explicit UTF-16 <-> UTF-8 conversion.
/// No guest address or Rust byte reference crosses this native UI boundary.
@MainActor
class WaylandInputView: UIView, UITextInput {
    var sendInput: (([String:Any]) -> Void)?
    var textContext: LocalWaylandText? {
        didSet { synchronizeDocument() }
    }
    var onFocus: (() -> Void)?
    private var document = ""
    private var selection = NSRange(location:0,length:0)
    private var cursorOffset = 0
    private var marked: NSRange?
    private var remoteSerial: UInt32?
    private var remoteEpoch: UInt64?
    private var forwardedKeys = Set<Int>()
    weak var inputDelegate: UITextInputDelegate?
    lazy var tokenizer: UITextInputTokenizer = UITextInputStringTokenizer(textInput:self)
    var markedTextStyle: [NSAttributedString.Key:Any]?
    var autocorrectionType: UITextAutocorrectionType = .no
    var autocapitalizationType: UITextAutocapitalizationType = .none
    var spellCheckingType: UITextSpellCheckingType = .no
    var isSecureTextEntry: Bool = false
    var keyboardType: UIKeyboardType = .default
    var returnKeyType: UIReturnKeyType = .default
    override var canBecomeFirstResponder: Bool { true }
    var hasText: Bool { !document.isEmpty }
    private func synchronizeDocument() {
        guard let context = textContext else {
            remoteSerial=nil; remoteEpoch=nil; document=""; selection=NSRange(location:0,length:0); cursorOffset=0; marked=nil
            return
        }
        let changedFocus = remoteEpoch != context.epoch
        guard changedFocus || remoteSerial != context.serial else { return }
        if !changedFocus && marked != nil { return }
        inputDelegate?.textWillChange(self); inputDelegate?.selectionWillChange(self)
        remoteSerial=context.serial; remoteEpoch=context.epoch
        document=context.text; marked=nil
        let cursor=utf16Offset(context.cursor,in:document), anchor=utf16Offset(context.anchor,in:document)
        cursorOffset=cursor
        selection=NSRange(location:min(cursor,anchor),length:abs(cursor-anchor))
        switch context.purpose {
        case 2: keyboardType = .numberPad
        case 3: keyboardType = .numbersAndPunctuation
        case 4: keyboardType = .phonePad
        case 5: keyboardType = .URL
        case 6: keyboardType = .emailAddress
        default: keyboardType = .default
        }
        inputDelegate?.textDidChange(self); inputDelegate?.selectionDidChange(self)
        isSecureTextEntry = (context.hint & 0xc0) != 0 || context.purpose == 8
        if changedFocus { reloadInputViews() }
    }
    private func utf16Offset(_ bytes: UInt32,in text:String)->Int {
        let index=text.utf8.index(text.utf8.startIndex,offsetBy:min(Int(bytes),text.utf8.count))
        guard let native=String.Index(index,within:text) else { return 0 }
        return native.utf16Offset(in:text)
    }
    private func byteOffset(_ offset:Int,in text:String)->Int {
        var at=max(0,min(offset,text.utf16.count))
        while at>0 && Range(NSRange(location:at,length:0),in:text)==nil {at-=1}
        let index=String.Index(utf16Offset:at,in:text)
        return text[..<index].utf8.count
    }
    private func valid(_ range:NSRange)->Bool {
        range.location >= 0 && range.length >= 0 && range.location <= document.utf16.count && range.length <= document.utf16.count-range.location && Range(range,in:document) != nil
    }
    private func edit(_ values:[String:Any]) {
        guard let context=textContext else { return }
        var command=values; command["op"]="text"; command["context"]=context.id;command["epoch"]=context.epoch;command["serial"]=context.serial
        sendInput?(command)
    }
    private func update(_ range:NSRange,with text:String) {
        guard let native=Range(range,in:document) else { return }
        document.replaceSubrange(native,with:text)
        selection=NSRange(location:range.location+text.utf16.count,length:0)
        cursorOffset=selection.location
    }
    var beginningOfDocument: UITextPosition { WaylandTextPosition(0) }
    var endOfDocument: UITextPosition { WaylandTextPosition(document.utf16.count) }
    var selectedTextRange: UITextRange? {
        get { WaylandTextRange(selection) }
        set { if let range=(newValue as? WaylandTextRange)?.value, valid(range) { selection=range } }
    }
    var markedTextRange: UITextRange? { marked.map(WaylandTextRange.init) }
    func text(in range:UITextRange)->String? {
        guard let range=(range as? WaylandTextRange)?.value,let native=Range(range,in:document) else { return nil }
        return String(document[native])
    }
    func insertText(_ text:String) {
        guard text.utf8.count<=4096 else { return }
        if textContext == nil { fallbackText(text);return }
        if let marked {
            edit(["preedit":"","commit":text]); update(marked,with:text);self.marked=nil
        } else { replace(WaylandTextRange(selection),withText:text) }
    }
    func deleteBackward() {
        if let marked {
            guard selection.location>=marked.location, NSMaxRange(selection)<=NSMaxRange(marked) else { return }
            let range:NSRange
            if selection.length>0 { range=selection }
            else {
                if selection.location==marked.location { return }
                let end=String.Index(utf16Offset:selection.location,in:document)
                range=NSRange(document.index(before:end)..<end,in:document)
            }
            update(range,with:"");self.marked=NSRange(location:marked.location,length:marked.length-range.length)
            sendPreedit();return
        }
        guard textContext != nil else { sendKey(14);return }
        if selection.length>0 { replace(WaylandTextRange(selection),withText:"");return }
        guard selection.location>0 else { return }
        let end=String.Index(utf16Offset:selection.location,in:document)
        let range=NSRange(document.index(before:end)..<end,in:document)
        replace(WaylandTextRange(range),withText:"")
    }
    func replace(_ range:UITextRange,withText text:String) {
        guard let range=(range as? WaylandTextRange)?.value,valid(range),text.utf8.count<=4096 else { return }
        let cursor=byteOffset(cursorOffset,in:document)
        let start=byteOffset(range.location,in:document),end=byteOffset(NSMaxRange(range),in:document)
        // Native replacement must overlap the current cursor/selection. Arbitrary
        // remote document edits are not expressible by text-input-v3.
        guard start<=cursor && end>=cursor else { return }
        if textContext?.version == 3 && selection.length>0 {
            let lower=byteOffset(selection.location,in:document),upper=byteOffset(NSMaxRange(selection),in:document)
            guard start<=lower && end>=upper else { return }
            // v3 deletes surrounding bytes excluding the selection; commit or
            // preedit already replaces selected text. v1 uses cursor-relative
            // byte ranges including selected text instead.
            edit(["delete_before":lower-start,"delete_after":end-upper,"commit":text])
        } else { edit(["delete_before":cursor-start,"delete_after":end-cursor,"commit":text]) }
        update(range,with:text);marked=nil
    }
    func setMarkedText(_ text:String?,selectedRange:NSRange) {
        guard textContext != nil else { return }
        let value=text ?? "";guard value.utf8.count<=4096 else { return }
        let range=marked ?? selection
        if marked == nil && selection.length>0 && textContext?.version != 3 {
            let cursor=byteOffset(cursorOffset,in:document)
            let start=byteOffset(selection.location,in:document),end=byteOffset(NSMaxRange(selection),in:document)
            edit(["delete_before":cursor-start,"delete_after":end-cursor])
        }
        update(range,with:value);marked=NSRange(location:range.location,length:value.utf16.count)
        let begin=min(max(0,selectedRange.location),value.utf16.count)
        let count=min(max(0,selectedRange.length),value.utf16.count-begin)
        selection=NSRange(location:range.location+begin,length:count);sendPreedit()
    }
    private func sendPreedit() {
        guard let marked,let range=Range(marked,in:document) else { return }
        let text=String(document[range])
        let start=byteOffset(selection.location-marked.location,in:text)
        let end=byteOffset(NSMaxRange(selection)-marked.location,in:text)
        edit(["preedit":text,"cursor_begin":start,"cursor_end":end])
    }
    func unmarkText() {
        guard let marked,let range=Range(marked,in:document) else { return }
        edit(["preedit":"","commit":String(document[range])]);self.marked=nil
    }
    func textRange(from:UITextPosition,to:UITextPosition)->UITextRange? {
        guard let a=from as? WaylandTextPosition,let b=to as? WaylandTextPosition else { return nil }
        let range=NSRange(location:min(a.offset,b.offset),length:abs(a.offset-b.offset))
        return valid(range) ? WaylandTextRange(range) : nil
    }
    func position(from:UITextPosition,offset:Int)->UITextPosition? {
        guard let p=from as? WaylandTextPosition else { return nil }
        let (next,overflow)=p.offset.addingReportingOverflow(offset)
        guard !overflow,(0...document.utf16.count).contains(next) else { return nil }
        return WaylandTextPosition(next)
    }
    func position(from:UITextPosition,in direction:UITextLayoutDirection,offset:Int)->UITextPosition? {
        position(from:from,offset:(direction == .left || direction == .up) ? -offset : offset)
    }
    func compare(_ position:UITextPosition,to other:UITextPosition)->ComparisonResult {
        let a=(position as? WaylandTextPosition)?.offset ?? 0,b=(other as? WaylandTextPosition)?.offset ?? 0
        return a==b ? .orderedSame : a<b ? .orderedAscending : .orderedDescending
    }
    func offset(from:UITextPosition,to:UITextPosition)->Int {
        ((to as? WaylandTextPosition)?.offset ?? 0)-((from as? WaylandTextPosition)?.offset ?? 0)
    }
    func position(within range:UITextRange,farthestIn direction:UITextLayoutDirection)->UITextPosition? {
        direction == .left || direction == .up ? range.start : range.end
    }
    func characterRange(byExtending position:UITextPosition,in direction:UITextLayoutDirection)->UITextRange? {
        guard let next=self.position(from:position,in:direction,offset:1) else { return nil }
        return textRange(from:position,to:next)
    }
    func baseWritingDirection(for position:UITextPosition,in direction:UITextStorageDirection)->NSWritingDirection { .natural }
    func setBaseWritingDirection(_ direction:NSWritingDirection,for range:UITextRange) {}
    func firstRect(for range:UITextRange)->CGRect { caretRect(for:range.start) }
    func caretRect(for position:UITextPosition)->CGRect {
        let rect=textContext?.caret ?? [0,0,1,20]
        guard rect.count==4 else { return .zero }
        return CGRect(x:CGFloat(rect[0]),y:CGFloat(rect[1]),width:CGFloat(max(1,rect[2])),height:CGFloat(max(1,rect[3])))
    }
    func selectionRects(for range:UITextRange)->[UITextSelectionRect] { [] }
    func closestPosition(to point:CGPoint)->UITextPosition? { WaylandTextPosition(selection.location) }
    func closestPosition(to point:CGPoint,within range:UITextRange)->UITextPosition? { range.start }
    func characterRange(at point:CGPoint)->UITextRange? { selectedTextRange }
    private var time: UInt32 { UInt32(truncatingIfNeeded:UInt64(ProcessInfo.processInfo.systemUptime*1000)) }
    private func sendKey(_ key:Int,modifiers:Int=0) {
        sendInput?(["op":"key","key":key,"pressed":true,"modifiers":modifiers,"time":time])
        sendInput?(["op":"key","key":key,"pressed":false,"modifiers":0,"time":time])
    }
    private func fallbackText(_ text:String) {
        for char in text {
            if char == "\n" { sendKey(28);continue }
            if char == "\t" { sendKey(15);continue }
            let lower=String(char).lowercased()
            if let key=Self.ascii[lower] { sendKey(key,modifiers:String(char) != lower ? 1 : 0) }
        }
    }
    private static let ascii: [String:Int] = {
        var keys:[String:Int]=[" ":57,"-":12,"=":13,"[":26,"]":27,";":39,"'":40,"`":41,"\\":43,",":51,".":52,"/":53]
        for (row,start) in [("1234567890",2),("qwertyuiop",16),("asdfghjkl",30),("zxcvbnm",44)] { for (i,c) in row.enumerated(){keys[String(c)]=start+i} }
        return keys
    }()
    private func evdev(_ hid:Int)->Int? {
        if (4...29).contains(hid) {return Self.ascii[String(UnicodeScalar(hid-4+97)!)]}
        if (30...38).contains(hid) {return hid-28}
        switch hid {
        case 39:return 11;case 40:return 28;case 41:return 1;case 42:return 14;case 43:return 15;case 44:return 57
        case 45:return 12;case 46:return 13;case 47:return 26;case 48:return 27;case 49:return 43;case 51:return 39;case 52:return 40;case 53:return 41;case 54:return 51;case 55:return 52;case 56:return 53;case 57:return 58
        case 58...67:return hid+1;case 68:return 87;case 69:return 88
        case 73:return 110;case 74:return 102;case 75:return 104;case 76:return 111;case 77:return 107;case 78:return 109;case 79:return 106;case 80:return 105;case 81:return 108;case 82:return 103
        case 224:return 29;case 225:return 42;case 226:return 56;case 227:return 125;case 228:return 97;case 229:return 54;case 230:return 100;case 231:return 126
        default:return nil
        }
    }
    private func modifiers(_ flags:UIKeyModifierFlags)->Int {
        (flags.contains(.shift) ? 1 : 0) | (flags.contains(.control) ? 4 : 0) | (flags.contains(.alternate) ? 8 : 0) | (flags.contains(.command) ? 64 : 0) | (flags.contains(.alphaShift) ? 2 : 0)
    }
    override func pressesBegan(_ presses:Set<UIPress>,with event:UIPressesEvent?) {
        var native=Set<UIPress>()
        for press in presses {
            guard let key=press.key,let code=evdev(key.keyCode.rawValue) else { native.insert(press);continue }
            let composed = textContext != nil && !key.modifierFlags.contains(.control) && !key.modifierFlags.contains(.command) && !key.characters.isEmpty && key.characters.unicodeScalars.allSatisfy { !CharacterSet.controlCharacters.contains($0) }
            if composed {native.insert(press);continue}
            forwardedKeys.insert(key.keyCode.rawValue)
            sendInput?(["op":"key","key":code,"pressed":true,"modifiers":modifiers(key.modifierFlags),"time":time])
        }
        if !native.isEmpty { super.pressesBegan(native,with:event) }
    }
    override func pressesEnded(_ presses:Set<UIPress>,with event:UIPressesEvent?) {
        var native=Set<UIPress>()
        for press in presses {
            guard let key=press.key,forwardedKeys.remove(key.keyCode.rawValue) != nil,let code=evdev(key.keyCode.rawValue) else { native.insert(press);continue }
            sendInput?(["op":"key","key":code,"pressed":false,"modifiers":modifiers(key.modifierFlags),"time":time])
        }
        if !native.isEmpty { super.pressesEnded(native,with:event) }
    }
    override func pressesCancelled(_ presses:Set<UIPress>,with event:UIPressesEvent?) { pressesEnded(presses,with:event) }
}
