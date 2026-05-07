// Generic OpenAI-compatible API client for word analysis

export async function analyzeWords(words, config) {
  if (!config?.apiKey || !config?.baseUrl) {
    throw new Error('API not configured');
  }

  const wordList = words.join(', ');
  const prompt = `Analyze these English words and return a JSON array. For each word provide:
- "word": the word (lowercase)
- "level": CEFR level (A1/A2/B1/B2/C1/C2)
- "zh": concise Chinese translation (2-4 chars preferred)

Words: ${wordList}

Return ONLY valid JSON array, no markdown, no explanation.`;

  const baseUrl = config.baseUrl.replace(/\/$/, '');
  const endpoint = baseUrl.includes('/v1') ? `${baseUrl}/chat/completions` : `${baseUrl}/v1/chat/completions`;

  const headers = {
    'Content-Type': 'application/json',
    'Authorization': `Bearer ${config.apiKey}`
  };

  // Anthropic uses x-api-key header
  if (baseUrl.includes('anthropic')) {
    headers['x-api-key'] = config.apiKey;
    headers['anthropic-version'] = '2023-06-01';
    delete headers['Authorization'];
  }

  const body = buildRequestBody(prompt, config);

  const res = await fetch(endpoint, {
    method: 'POST',
    headers,
    body: JSON.stringify(body)
  });

  if (!res.ok) {
    const err = await res.text();
    throw new Error(`API error ${res.status}: ${err}`);
  }

  const data = await res.json();
  const text = extractContent(data);

  return parseWordResults(text, words);
}

function buildRequestBody(prompt, config) {
  const model = config.model || 'gpt-4o-mini';

  if (config.baseUrl?.includes('anthropic')) {
    return {
      model,
      max_tokens: 4096,
      messages: [{ role: 'user', content: prompt }],
      system: 'You are a language analysis assistant. Always respond with valid JSON only, no markdown.'
    };
  }

  return {
    model,
    messages: [
      { role: 'system', content: 'You are a language analysis assistant. Always respond with valid JSON only, no markdown.' },
      { role: 'user', content: prompt }
    ],
    temperature: 0,
    max_tokens: 4096,
    response_format: supportsJsonMode(model) ? { type: 'json_object' } : undefined
  };
}

function supportsJsonMode(model) {
  return model.includes('gpt-4') || model.includes('gpt-3.5');
}

function extractContent(data) {
  // OpenAI format
  if (data.choices?.[0]?.message?.content) {
    return data.choices[0].message.content;
  }
  // Anthropic format
  if (data.content?.[0]?.text) {
    return data.content[0].text;
  }
  return '';
}

function parseWordResults(text, originalWords) {
  // Extract JSON array from response (handles code blocks too)
  const match = text.match(/\[[\s\S]*\]/);
  if (!match) {
    // Try to parse entire text as JSON
    try {
      const parsed = JSON.parse(text);
      if (Array.isArray(parsed)) return parsed;
    } catch {}
    console.warn('Could not parse API response:', text);
    return [];
  }

  try {
    const results = JSON.parse(match[0]);
    return results.filter(r => r.word && r.level && r.zh);
  } catch (e) {
    console.warn('JSON parse failed:', e, text);
    return [];
  }
}

// Chunk array into batches
export function chunkArray(arr, size) {
  const chunks = [];
  for (let i = 0; i < arr.length; i += size) {
    chunks.push(arr.slice(i, i + size));
  }
  return chunks;
}

// Test API connection
export async function testApiConnection(config) {
  try {
    const results = await analyzeWords(['serendipity', 'cat'], config);
    return results.length > 0;
  } catch (e) {
    throw e;
  }
}
