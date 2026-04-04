#include <string>
#include <iostream>

std::string JsonGetStringField(const std::string &json, const std::string &key, size_t startPos = 0) {
  const std::string pattern = "\"" + key + "\"";
  size_t keyPos = json.find(pattern, startPos);
  if (keyPos == std::string::npos) return "";

  size_t colonPos = json.find(':', keyPos + pattern.size());
  if (colonPos == std::string::npos) return "";

  size_t valueStart = json.find('"', colonPos);
  if (valueStart == std::string::npos) return "";

  size_t valueEnd = valueStart + 1;
  while (valueEnd < json.size()) {
    if (json[valueEnd] == '"' && json[valueEnd - 1] != '\\') {
      break;
    }
    ++valueEnd;
  }

  if (valueEnd >= json.size()) return "";
  return json.substr(valueStart + 1, valueEnd - valueStart - 1);
}

int main() {
    std::string test = "{\"type\":\"peer-joined\",\"peer\":{\"id\":\"peer_gcgja0uql\",\"name\":\"Desktop Client\",\"fingerprint\":null,\"publicKey\":null,\"clientType\":\"unknown\"}}";
    std::cout << "id: " << JsonGetStringField(test, "id") << std::endl;
    std::cout << "name: " << JsonGetStringField(test, "name") << std::endl;
    
    std::string testPeers = "{\"type\":\"peers\",\"peers\":[{\"id\":\"peer_mhex6zl1d\",\"name\":\"Web Client\",\"fingerprint\":null,\"publicKey\":null,\"isLan\":false,\"clientType\":\"web\"}]}";
    std::cout << "peers payload test:" << std::endl;
    size_t pos = 0;
    while ((pos = testPeers.find("\"id\":", pos)) != std::string::npos) {
        size_t idStart = testPeers.find('"', pos + 5);
        size_t idEnd = testPeers.find('"', idStart + 1);
        if (idStart != std::string::npos && idEnd != std::string::npos) {
            std::cout << "peer.id: " << testPeers.substr(idStart + 1, idEnd - idStart - 1) << std::endl;
        }
        size_t namePos = testPeers.find("\"name\":", pos);
        if (namePos != std::string::npos && namePos < pos + 200) {
            size_t nameStart = testPeers.find('"', namePos + 7);
            size_t nameEnd = testPeers.find('"', nameStart + 1);
            if (nameStart != std::string::npos && nameEnd != std::string::npos) {
                std::cout << "peer.name: " << testPeers.substr(nameStart + 1, nameEnd - nameStart - 1) << std::endl;
            }
        }
        pos = idEnd + 1;
    }
    return 0;
}
