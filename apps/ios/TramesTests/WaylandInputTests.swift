// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
import UIKit
import TramesKit
@testable import Trames

final class WaylandInputTests: XCTestCase {
    @MainActor
    func testHeldShortcutDoesNotSendDuplicateDownsAfterSystemPasteAlert() {
        let view=WaylandInputView();var commands:[[String:Any]]=[]
        view.sendInput={ commands.append($0) }
        XCTAssertTrue(view.hardwareKey(25,characters:"v",modifiers:.control,pressed:true))
        // UIKit may deliver repeated downs while the native permission modal
        // owns the physical key-up. The compositor must receive one gesture.
        for _ in 0..<50 {
            XCTAssertTrue(view.hardwareKey(25,characters:"v",modifiers:.control,pressed:true))
        }
        XCTAssertEqual(commands.count,1)
        XCTAssertTrue(view.hardwareKey(25,pressed:false))
        XCTAssertEqual(commands.compactMap{$0["pressed"] as? Bool},[true,false])
        // A later intentional paste and held navigation remain usable. Their
        // normal repetition is implemented by the Wayland client, not UIKit.
        for hid in [25,80] {
            commands=[]
            XCTAssertTrue(view.hardwareKey(hid,modifiers:.control,pressed:true))
            XCTAssertTrue(view.hardwareKey(hid,modifiers:.control,pressed:true))
            XCTAssertTrue(view.hardwareKey(hid,pressed:false))
            XCTAssertEqual(commands.compactMap{$0["pressed"] as? Bool},[true,false])
        }
    }
    @MainActor
    func testHardwareNavigationKeepsGuestCursorAndSubsequentCompositionTogether() throws {
        var object:[String:Any] = ["version":3,"id":7,"epoch":3,"surface":4,"window":4,"serial":8,
            "text":"ab","cursor":2,"anchor":2,"caret":[0,0,1,20],"hint":0,"purpose":0]
        let view=WaylandInputView();var commands:[[String:Any]]=[]
        view.sendInput={ commands.append($0) }
        view.textContext=try JSONDecoder().decode(LocalWaylandText.self,from:JSONSerialization.data(withJSONObject:object))
        // These are the actual UIKit private-use arrow strings, which are not
        // members of CharacterSet.controlCharacters.
        for (hid,string,evdev) in [(80,UIKeyCommand.inputLeftArrow,105),(79,UIKeyCommand.inputRightArrow,106),
                                   (82,UIKeyCommand.inputUpArrow,103),(81,UIKeyCommand.inputDownArrow,108),
                                   (74,"\u{F729}",102),(77,"\u{F72B}",107),(58,"\u{F704}",59)] {
            commands=[]
            XCTAssertTrue(view.hardwareKey(hid,characters:string,pressed:true))
            XCTAssertTrue(view.hardwareKey(hid,pressed:false))
            XCTAssertEqual(commands.compactMap{$0["key"] as? Int},[evdev,evdev])
            XCTAssertEqual(commands.compactMap{$0["pressed"] as? Bool},[true,false])
        }
        let nonText:[(Int,Int)] = [(40,28),(41,1),(42,14),(43,15),(57,58),(73,110),(74,102),(75,104),(76,111),(77,107),(78,109)]
            + Array(zip(58...67,59...68)) + [(68,87),(69,88),(70,99),(71,70),(72,119),(83,69),(88,96),(101,127)]
            + [(224,29),(225,42),(226,56),(227,125),(228,97),(229,54),(230,100),(231,126)]
        for (hid,evdev) in nonText {
            commands=[]
            XCTAssertTrue(view.hardwareKey(hid,characters:"\u{F700}",pressed:true))
            XCTAssertTrue(view.hardwareKey(hid,pressed:false))
            XCTAssertEqual(commands.compactMap{$0["key"] as? Int},[evdev,evdev],"HID \(hid)")
        }
        for (flags,mask) in [(UIKeyModifierFlags.shift,1),(.control,4),(.alternate,8),(.command,64),(.alphaShift,2),([.control,.shift],5)] {
            commands=[]
            XCTAssertTrue(view.hardwareKey(80,characters:UIKeyCommand.inputLeftArrow,modifiers:flags,pressed:true))
            XCTAssertTrue(view.hardwareKey(80,pressed:false))
            XCTAssertEqual(commands.compactMap{$0["modifiers"] as? Int},[mask,0])
        }
        XCTAssertFalse(view.hardwareKey(4,characters:"a",pressed:true),"Printable text belongs to the native input method")
        XCTAssertTrue(view.hardwareKey(6,characters:"c",modifiers:.control,pressed:true))
        XCTAssertTrue(view.hardwareKey(6,modifiers:.control,pressed:false))
        // GTK reports its cursor after handling Left. Text must still insert
        // without a mouse click or a new focus/epoch.
        object["serial"]=9;object["cursor"]=1;object["anchor"]=1
        view.textContext=try JSONDecoder().decode(LocalWaylandText.self,from:JSONSerialization.data(withJSONObject:object))
        commands=[];view.insertText("你🙂")
        XCTAssertEqual(commands.count,1);XCTAssertEqual(commands[0]["commit"] as? String,"你🙂")
        XCTAssertEqual(commands[0]["serial"] as? UInt32,9)
        let all=try XCTUnwrap(view.textRange(from:view.beginningOfDocument,to:view.endOfDocument))
        XCTAssertEqual(view.text(in:all),"a你🙂b")
    }
    @MainActor
    func testSoftKeyboardPunctuationAndRawKeypadUseStandardLinuxKeys() {
        let view=WaylandInputView();var commands:[[String:Any]]=[]
        view.sendInput={ commands.append($0) }
        view.insertText("aA1!_+{}:\"~|<>?\t\n")
        let downs=commands.filter{$0["pressed"] as? Bool == true}
        XCTAssertEqual(downs.compactMap{$0["key"] as? Int},[30,30,2,2,12,13,26,27,39,40,41,43,51,52,53,15,28])
        XCTAssertEqual(downs.compactMap{$0["modifiers"] as? Int},[0,1,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0])
        let raw:[(Int,Int)] = [(50,43),(84,98),(85,55),(86,74),(87,78),(88,96)]
            + Array(zip(89...97,[79,80,81,75,76,77,71,72,73])) + [(98,82),(99,83),(100,86),(103,117)]
        for (hid,evdev) in raw {
            commands=[]
            XCTAssertTrue(view.hardwareKey(hid,characters:"1",pressed:true))
            XCTAssertTrue(view.hardwareKey(hid,pressed:false))
            XCTAssertEqual(commands.compactMap{$0["key"] as? Int},[evdev,evdev])
        }
    }
    @MainActor
    func testV3SelectionIsReplacedWithoutDeletingItTwice() throws {
        let object:[String:Any] = ["version":3,"id":7,"epoch":3,"surface":4,"window":4,"serial":8,
            "text":"a🙂中","cursor":8,"anchor":1,"caret":[0,0,1,20],"hint":0,"purpose":0]
        let context=try JSONDecoder().decode(LocalWaylandText.self,from:JSONSerialization.data(withJSONObject:object))
        let view=WaylandInputView(frame:.zero)
        var commands:[[String:Any]]=[];view.sendInput={commands.append($0)}
        view.textContext=context;view.insertText("新")
        XCTAssertEqual(commands.last?["delete_before"] as? Int,0)
        XCTAssertEqual(commands.last?["delete_after"] as? Int,0)
        XCTAssertEqual(commands.last?["commit"] as? String,"新")
        var updated=object;updated["epoch"]=4
        view.textContext=try JSONDecoder().decode(LocalWaylandText.self,from:JSONSerialization.data(withJSONObject:updated))
        commands=[];view.setMarkedText("你",selectedRange:NSRange(location:1,length:0))
        XCTAssertEqual(commands.count,1)
        XCTAssertEqual(commands.last?["preedit"] as? String,"你")
    }
    @MainActor
    func testMarkedBackspaceUsesCompositionCaret() throws {
        let object:[String:Any] = ["version":1,"id":7,"epoch":3,"surface":4,"window":4,"serial":8,
            "text":"","cursor":0,"anchor":0,"caret":[0,0,1,20],"hint":0,"purpose":0]
        let view=WaylandInputView(frame:.zero)
        var commands:[[String:Any]]=[];view.sendInput={commands.append($0)}
        view.textContext=try JSONDecoder().decode(LocalWaylandText.self,from:JSONSerialization.data(withJSONObject:object))
        view.setMarkedText("a🙂b",selectedRange:NSRange(location:3,length:0));view.deleteBackward()
        XCTAssertEqual(commands.last?["preedit"] as? String,"ab")
        XCTAssertEqual(commands.last?["cursor_begin"] as? Int,1)
    }
    @MainActor
    func testSelectionReplacementUsesActiveCursorInsteadOfSortedRangeStart() throws {
        let object:[String:Any] = ["version":1,"id":7,"epoch":3,"surface":4,"window":4,"serial":8,
            "text":"a🙂中","cursor":8,"anchor":1,"caret":[0,0,1,20],"hint":0,"purpose":0]
        let view=WaylandInputView(frame:.zero)
        var commands:[[String:Any]]=[];view.sendInput={commands.append($0)}
        view.textContext=try JSONDecoder().decode(LocalWaylandText.self,from:JSONSerialization.data(withJSONObject:object))
        view.insertText("新")
        XCTAssertEqual(commands.last?["delete_before"] as? Int,7)
        XCTAssertEqual(commands.last?["delete_after"] as? Int,0)
        let all=try XCTUnwrap(view.textRange(from:view.beginningOfDocument,to:view.endOfDocument))
        XCTAssertEqual(view.text(in:all),"a新")
    }
    @MainActor
    func testNativeCompositionAndUTF8OffsetsPreserveEmoji() throws {
        let object:[String:Any] = ["version":1,"id":7,"epoch":3,"surface":4,"window":4,"serial":8,
            "text":"a🙂中","cursor":5,"anchor":5,"caret":[30,40,1,20],"hint":0,"purpose":0]
        let context = try JSONDecoder().decode(LocalWaylandText.self,from:JSONSerialization.data(withJSONObject:object))
        let view=WaylandInputView(frame:CGRect(x:0,y:0,width:640,height:480))
        var commands:[[String:Any]]=[]
        view.sendInput = {commands.append($0)}
        view.textContext=context
        XCTAssertEqual(view.offset(from:view.beginningOfDocument,to:try XCTUnwrap(view.selectedTextRange).start),3)
        view.setMarkedText("ni",selectedRange:NSRange(location:2,length:0))
        XCTAssertEqual(commands.last?["preedit"] as? String,"ni")
        XCTAssertEqual(commands.last?["cursor_begin"] as? Int,2)
        view.insertText("你🙂")
        XCTAssertNil(view.markedTextRange)
        XCTAssertEqual(commands.last?["commit"] as? String,"你🙂")
        XCTAssertEqual(commands.last?["serial"] as? UInt32,8)
        let all=try XCTUnwrap(view.textRange(from:view.beginningOfDocument,to:view.endOfDocument))
        XCTAssertEqual(view.text(in:all),"a🙂你🙂中")
        view.deleteBackward()
        XCTAssertEqual(commands.last?["delete_before"] as? Int,4)
        XCTAssertEqual(view.caretRect(for:view.beginningOfDocument),CGRect(x:30,y:40,width:1,height:20))
    }
    @MainActor
    func testEndingMarkedTextCommitsOnceAndFocusResetClearsIt() throws {
        var object:[String:Any] = ["version":1,"id":7,"epoch":3,"surface":4,"window":4,"serial":8,
            "text":"","cursor":0,"anchor":0,"caret":[0,0,1,20],"hint":0,"purpose":0]
        let view=WaylandInputView(frame:.zero)
        var commands:[[String:Any]]=[]
        view.sendInput={commands.append($0)}
        view.textContext=try JSONDecoder().decode(LocalWaylandText.self,from:JSONSerialization.data(withJSONObject:object))
        view.setMarkedText("中文",selectedRange:NSRange(location:2,length:0))
        view.unmarkText();view.unmarkText()
        XCTAssertEqual(commands.filter{$0["commit"] != nil}.count,1)
        view.setMarkedText("pending",selectedRange:NSRange(location:7,length:0))
        object["epoch"]=4;object["text"]="other";object["cursor"]=5;object["anchor"]=5
        view.textContext=try JSONDecoder().decode(LocalWaylandText.self,from:JSONSerialization.data(withJSONObject:object))
        XCTAssertNil(view.markedTextRange)
        let all=try XCTUnwrap(view.textRange(from:view.beginningOfDocument,to:view.endOfDocument))
        XCTAssertEqual(view.text(in:all),"other")
    }
    @MainActor
    func testNativeClipboardActionsRouteToGuestControlKeys() {
        let view=WaylandInputView();var events:[[String:Any]]=[]
        view.sendInput={ events.append($0) }
        view.copy(nil);view.cut(nil);view.paste(nil);view.selectAll(nil)
        XCTAssertEqual(events.compactMap {$0["key"] as? Int},[46,46,45,45,47,47,30,30])
        XCTAssertEqual(events.compactMap {$0["modifiers"] as? Int},[4,0,4,0,4,0,4,0])
    }

}
