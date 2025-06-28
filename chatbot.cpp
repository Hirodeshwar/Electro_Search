#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <iostream>
#include <string>
#include "json.hpp" // Include the json library

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")

using namespace std;
using json = nlohmann::json;

const string GEMINI_API_KEY = "AIzaSyCWjluiNN2JwWQKGjI3BBD6y2pPa0iDlQQ"; // Replace with actual API key

// Function to escape JSON special characters
string escapeJson(const string& s) {
    string result;
    for (char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c;
        }
    }
    return result;
}

// Function to extract relevant response from Gemini API
string extractRelevantResponse(const string &rawResponse) {
    try {
        json jsonResponse = json::parse(rawResponse);
        if (jsonResponse.contains("candidates") && jsonResponse["candidates"].is_array()) {
            json firstCandidate = jsonResponse["candidates"][0];
            if (firstCandidate.contains("content") && firstCandidate["content"].contains("parts") &&
                firstCandidate["content"]["parts"].is_array() && !firstCandidate["content"]["parts"].empty()) {
                string responseText = firstCandidate["content"]["parts"][0]["text"].get<string>();
                
                // Format response for new lines and bullet points
                size_t pos = 0;
                while ((pos = responseText.find("\n- ", pos)) != string::npos) {
                    responseText.replace(pos, 3, "\n\n- ");
                    pos += 4;
                }
                return responseText;
            }
        }
    } catch (const exception &e) {
        return "Error parsing response: " + string(e.what());
    }
    return "Invalid response from AI.";
}

// Function to make an API request using WinHTTP
string getGeminiResponse(const string &input) {
    HINTERNET hSession = WinHttpOpen(L"Chatbot", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return "Error: Could not initialize session";

    HINTERNET hConnect = WinHttpConnect(hSession, L"generativelanguage.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) return "Error: Could not connect";

    wstring apiPath = L"/v1/models/gemini-1.5-flash:generateContent?key=" + wstring(GEMINI_API_KEY.begin(), GEMINI_API_KEY.end());

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", apiPath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) return "Error: Could not create request";

    string requestData = R"({"contents":[{"parts":[{"text":")" + escapeJson(input) + R"("}]}]})";
    const WCHAR *headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";

    BOOL requestSent = WinHttpSendRequest(hRequest, headers, -1, (LPVOID)requestData.c_str(), requestData.size(), requestData.size(), 0);
    if (!requestSent) return "Error: Request could not be sent";

    BOOL responseReceived = WinHttpReceiveResponse(hRequest, NULL);
    if (!responseReceived) return "Error: No response received";

    DWORD bytesRead;
    char response[8192] = {0};  // Increased buffer size
    BOOL readData = WinHttpReadData(hRequest, response, sizeof(response) - 1, &bytesRead);
    if (!readData || bytesRead == 0) return "Error: Empty response";

    string rawResponse(response, bytesRead);

    // ✅ Debug: Print raw JSON response
    cout << "\n📦 Raw Gemini API Response:\n" << rawResponse << "\n" << endl;

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    // ✅ Optional: Log the parsed error message if invalid
    string parsed = extractRelevantResponse(rawResponse);
    if (parsed == "Invalid response from AI.") {
        cout << "⚠️ Gemini JSON structure not as expected." << endl;
    }
    return parsed;
}

// Function to start the HTTP server
void startServer() {
    WSADATA wsaData;
    SOCKET serverSocket, clientSocket;
    sockaddr_in serverAddr, clientAddr;
    char buffer[4096];

    WSAStartup(MAKEWORD(2, 2), &wsaData);
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    bind(serverSocket, (sockaddr *)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, 5);

    cout << "Server started on http://localhost:8080" << endl;

    while (true) {
        int clientSize = sizeof(clientAddr);
        clientSocket = accept(serverSocket, (sockaddr *)&clientAddr, &clientSize);
        memset(buffer, 0, sizeof(buffer));
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            closesocket(clientSocket);
            continue;
        }

        string request(buffer, bytesReceived);
        string userQuery;

        if (request.find("POST") != string::npos) {
            size_t bodyStart = request.find("\r\n\r\n");
            if (bodyStart != string::npos) {
                string body = request.substr(bodyStart + 4);
                json requestBody = json::parse(body, nullptr, false);
                if (!requestBody.is_discarded() && requestBody.contains("query")) {
                    userQuery = requestBody["query"].get<string>();
                }
            }
        }

        if (userQuery.empty()) userQuery = "Give details about an electrical device.";

        cout << "User Query: " << userQuery << endl;
        string responseText = getGeminiResponse("Provide a comprehensive technical description of the electrical device: " + userQuery + ". Include specifications, working principles, applications, and relevant formulas. " +"Explain how it is used in electrical engineering and provide any relevant industry standards or safety precautions. " +"If the query is not related to an electrical device, clearly state that it is not an electrical device.");
        

        json jsonResponse;
        jsonResponse["reply"] = responseText;

        string httpResponse =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "\r\n" + jsonResponse.dump(4);

        send(clientSocket, httpResponse.c_str(), httpResponse.size(), 0);
        closesocket(clientSocket);
    }

    WSACleanup();
}

int main() {
    cout << "Starting HTTP server on http://localhost:8080..." << endl;
    startServer();
    return 0;
}
