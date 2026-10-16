// SPDX-License-Identifier: GPL-3.0-or-later
import XCTest
import TramesKit
@testable import Trames

final class WaylandInputTests: XCTestCase {
    @MainActor
    func testSelectionReplacementUsesActiveCursorInsteadOfSortedRangeStart() throws {
        let object:[String:Any] = ["id":7,"epoch":3,"surface":4,"window":4,"serial":8,
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
        let object:[String:Any] = ["id":7,"epoch":3,"surface":4,"window":4,"serial":8,
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
        var object:[String:Any] = ["id":7,"epoch":3,"surface":4,"window":4,"serial":8,
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
}
