// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
#if canImport(Security)
import Security
public enum Secrets {
    private static func query(_ account: String) -> [String:Any] {
        [kSecClass as String:kSecClassGenericPassword, kSecAttrService as String:"org.trames.ssh", kSecAttrAccount as String:account]
    }
    public static func load(_ account: String) throws -> String? {
        var q = query(account); q[kSecReturnData as String] = true; q[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?
        let status = SecItemCopyMatching(q as CFDictionary, &result)
        if status == errSecItemNotFound { return nil }
        guard status == errSecSuccess, let data = result as? Data, let text = String(data: data, encoding: .utf8) else { throw TramesError.message("Keychain read failed (\(status))") }
        return text
    }
    public static func save(_ value: String, account: String) throws {
        guard value.utf8.count <= 65536 else { throw TramesError.message("Credential exceeds 64 KiB") }
        let q = query(account)
        var attributes: [String:Any] = [kSecValueData as String:Data(value.utf8), kSecAttrAccessible as String:kSecAttrAccessibleWhenUnlockedThisDeviceOnly]
        let update = SecItemUpdate(q as CFDictionary, attributes as CFDictionary)
        if update == errSecItemNotFound {
            attributes.merge(q) { value,_ in value }
            let status = SecItemAdd(attributes as CFDictionary,nil)
            guard status == errSecSuccess else { throw TramesError.message("Keychain save failed (\(status))") }
        } else if update != errSecSuccess { throw TramesError.message("Keychain update failed (\(update))") }
    }
    public static func delete(_ account: String) throws {
        let status = SecItemDelete(query(account) as CFDictionary)
        guard status == errSecSuccess || status == errSecItemNotFound else { throw TramesError.message("Keychain delete failed (\(status))") }
    }
}
#endif
