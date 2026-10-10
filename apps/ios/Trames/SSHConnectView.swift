// SPDX-License-Identifier: GPL-3.0-or-later
import SwiftUI
import TramesKit
struct SSHConnectView: View {
    @ObservedObject var workspace: WorkspaceModel
    @Environment(\.dismiss) private var dismiss
    @State private var alias = ""
    @State private var user = "user"
    @State private var password = ""
    @State private var passphrase = ""
    @State private var configText = ""
    @State private var remember = false
    @State private var useKey = false
    @State private var loading = false
    @State private var error: String?
    @State private var warnings: [String] = []
    var body: some View {
        NavigationStack {
            Form {
                Section("主机") {
                    TextField("主机别名或地址", text: $alias).textInputAutocapitalization(.never).autocorrectionDisabled()
                    TextField("默认用户名", text: $user).textInputAutocapitalization(.never).autocorrectionDisabled()
                    Button("读取 ~/.ssh/config") { loadConfig() }.disabled(loading)
                    if !configText.isEmpty { Text(configText).font(.system(.caption, design: .monospaced)).textSelection(.enabled) }
                }
                Section("认证") {
                    Toggle("使用配置中的 Ed25519 IdentityFile", isOn: $useKey)
                    if useKey { SecureField("密钥口令（可选）", text: $passphrase) }
                    else { SecureField("密码", text: $password) }
                    Toggle("将密码或私钥保存到本机 Keychain", isOn: $remember)
                    Button("删除此主机保存的凭据") { removeSaved() }
                }
                if let error { Section { Text(error).foregroundStyle(.red) } }
                Section { Button(loading ? "正在连接…" : "连接") { connect(confirmWarnings: false) }.disabled(loading || alias.isEmpty) }
            }
            .navigationTitle("SSH 连接")
            .toolbar { Button("取消") { password = ""; passphrase = ""; dismiss() } }
            .confirmationDialog("以下 SSH 指令尚不支持", isPresented: Binding(get: { !warnings.isEmpty }, set: { if !$0 { warnings = [] } }), titleVisibility: .visible) {
                Button("使用受支持的配置连接") { connect(confirmWarnings: true) }
                Button("取消", role: .cancel) { warnings = [] }
            } message: { Text(warnings.joined(separator: "\n")) }
        }
    }
    private func loadConfig() {
        loading = true
        Task {
            defer { loading = false }
            do { configText = try await workspace.guestText("/home/user/.ssh/config"); error = nil }
            catch { self.error = error.localizedDescription }
        }
    }
    private func connect(confirmWarnings: Bool) {
        loading = true; error = nil
        Task {
            defer { loading = false }
            do {
                let host = try SSHConfig(configText).resolve(alias, defaultUser: user)
                if !confirmWarnings && !host.warnings.isEmpty { warnings = host.warnings; return }
                warnings = []
                let account = "\(host.user)@\(host.endpoint):\(useKey ? "ed25519" : "password")"
                let credential: SSHCredential
                if useKey {
                    var key = try Secrets.load(account)
                    if key == nil {
                        guard let identity = host.identityFile else { throw TramesError.message("请在 ~/.ssh/config 中指定 Ed25519 IdentityFile") }
                        let path = identity.hasPrefix("~/") ? "/home/user/" + identity.dropFirst(2) : identity
                        guard path.hasPrefix("/") else { throw TramesError.message("IdentityFile 需要绝对路径或 ~/ 路径") }
                        key = try await workspace.guestText(path)
                    }
                    if remember { try Secrets.save(key!, account: account) }
                    credential = .ed25519(key!, passphrase: passphrase.isEmpty ? nil : passphrase)
                } else {
                    let secret = password.isEmpty ? try Secrets.load(account) ?? "" : password
                    guard !secret.isEmpty else { throw TramesError.message("请输入密码") }
                    if remember { try Secrets.save(secret, account: account) }
                    credential = .password(secret)
                }
                workspace.connect(host: host, credential: credential)
                password = ""; passphrase = ""; dismiss()
            } catch { self.error = error.localizedDescription }
        }
    }
    private func removeSaved() {
        do {
            let host = try SSHConfig(configText).resolve(alias, defaultUser: user)
            for type in ["password", "ed25519"] { try Secrets.delete("\(host.user)@\(host.endpoint):\(type)") }
            password = ""; error = "已删除保存的凭据"
        } catch { self.error = error.localizedDescription }
    }
}
