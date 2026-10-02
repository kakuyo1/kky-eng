<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>lens::llm</name>
    <message>
        <location filename="../src/llm/llm_pure.cpp" line="93"/>
        <source>The model answered with something that is not a JSON object.</source>
        <translation>模型返回的内容不是一个 JSON 对象。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_pure.cpp" line="99"/>
        <source>The model&apos;s answer carries no choices.</source>
        <translation>模型返回的内容里没有 choices。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_pure.cpp" line="107"/>
        <source>The model stopped before finishing (reason: %1).</source>
        <translation>模型未正常结束（原因：%1）。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_pure.cpp" line="111"/>
        <source>absent</source>
        <translation>缺失</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_pure.cpp" line="117"/>
        <source>The model&apos;s answer is not valid JSON.</source>
        <translation>模型返回的内容不是合法 JSON。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_pure.cpp" line="122"/>
        <source>The model&apos;s answer has no results array.</source>
        <translation>模型返回的内容里没有 results 数组。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_pure.cpp" line="131"/>
        <source>One of the results entries is not an object.</source>
        <translation>results 里有一项不是对象。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_pure.cpp" line="137"/>
        <source>A results entry is missing the field &quot;%1&quot;.</source>
        <translation>results 里有一项缺少字段 “%1”。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_pure.cpp" line="149"/>
        <source>A results entry has an empty field (word=%1).</source>
        <translation>results 里有一项字段为空（word=%1）。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_pure.cpp" line="154"/>
        <source>The model echoed the same word twice: %1.</source>
        <translation>模型重复回显了同一个词：%1。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_pure.cpp" line="161"/>
        <source>The model echoed %1 word(s) for the %2 that were asked for.</source>
        <translation>请求了 %2 个词，模型回显了 %1 个。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_pure.cpp" line="173"/>
        <source>The model never echoed &quot;%1&quot;.</source>
        <translation>模型没有回显 “%1”。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_client.cpp" line="25"/>
        <source>The request was rejected as malformed (400).</source>
        <translation>请求格式有误，被服务拒绝（400）。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_client.cpp" line="28"/>
        <source>The API key is missing or not accepted (401).</source>
        <translation>API key 缺失或未被接受（401）。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_client.cpp" line="31"/>
        <source>The account is out of credit (402).</source>
        <translation>账户余额不足（402）。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_client.cpp" line="34"/>
        <source>The request parameters were rejected (422).</source>
        <translation>请求参数被拒绝（422）。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_client.cpp" line="37"/>
        <source>Too many requests; the service is rate-limiting (429).</source>
        <translation>请求过于频繁，已被限流（429）。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_client.cpp" line="40"/>
        <source>The explanation service failed (500).</source>
        <translation>释义服务出错（500）。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_client.cpp" line="43"/>
        <source>The explanation service is overloaded (503).</source>
        <translation>释义服务过载（503）。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_client.cpp" line="46"/>
        <source>Unexpected HTTP status %1.</source>
        <translation>意外的 HTTP 状态码 %1。</translation>
    </message>
</context>
<context>
    <name>lens::llm::LlmClient</name>
    <message>
        <location filename="../src/llm/llm_client.cpp" line="73"/>
        <source>There is nothing to look up.</source>
        <translation>没有待查内容。</translation>
    </message>
    <message>
        <location filename="../src/llm/llm_client.cpp" line="104"/>
        <source>The request could not reach the service: %1</source>
        <translation>请求未能送达服务：%1</translation>
    </message>
</context>
</TS>
