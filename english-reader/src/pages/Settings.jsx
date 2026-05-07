import React, { useState } from 'react';
import { useApp } from '../App.jsx';
import { LEVELS, LEVEL_INFO } from '../utils/cefrWords.js';
import { testApiConnection } from '../utils/api.js';

export default function Settings() {
  const { theme, setTheme, cefrLevel, setCefrLevel, apiConfig, setApiConfig, fontSize, setFontSize } = useApp();
  const [testing, setTesting] = useState(false);
  const [testResult, setTestResult] = useState(null);
  const [showKey, setShowKey] = useState(false);

  async function handleTestApi() {
    if (!apiConfig.apiKey) {
      setTestResult({ ok: false, msg: '请先填写 API Key' });
      return;
    }
    setTesting(true);
    setTestResult(null);
    try {
      await testApiConnection(apiConfig);
      setTestResult({ ok: true, msg: '连接成功！' });
    } catch (e) {
      setTestResult({ ok: false, msg: e.message });
    } finally {
      setTesting(false);
    }
  }

  function updateConfig(field, value) {
    setApiConfig({ ...apiConfig, [field]: value });
  }

  return (
    <div className="page">
      <div style={{padding:'16px 16px 8px',background:'var(--surface)',borderBottom:'1px solid var(--border)',position:'sticky',top:0,zIndex:10}}>
        <h1 style={{fontSize:22,fontWeight:700}}>设置</h1>
      </div>

      {/* CEFR Level */}
      <div className="settings-section">
        <div className="settings-section-title">英语水平</div>
        <div style={{background:'var(--surface)',borderTop:'1px solid var(--border)',borderBottom:'1px solid var(--border)',padding:'12px 0 4px'}}>
          <div style={{padding:'0 16px 4px',fontSize:14,color:'var(--text-secondary)'}}>
            阅读器将标注超过此等级的单词
          </div>
          <div className="level-chips">
            {LEVELS.map(l => (
              <button
                key={l}
                className={`level-chip ${cefrLevel === l ? 'active' : ''}`}
                style={cefrLevel === l ? {background:LEVEL_INFO[l].color, borderColor:LEVEL_INFO[l].color} : {}}
                onClick={() => setCefrLevel(l)}
              >
                {l}
              </button>
            ))}
          </div>
          <div style={{padding:'4px 16px 12px',fontSize:13,color:'var(--text-secondary)'}}>
            当前：<strong>{LEVEL_INFO[cefrLevel]?.label}</strong> — {LEVEL_INFO[cefrLevel]?.desc}
          </div>
        </div>
      </div>

      {/* API Config */}
      <div className="settings-section">
        <div className="settings-section-title">AI API 配置</div>
        <div style={{background:'var(--surface)',borderTop:'1px solid var(--border)',borderBottom:'1px solid var(--border)'}}>
          <div className="settings-item">
            <span className="settings-item-label">服务地址</span>
            <input
              className="settings-item-input"
              type="url"
              placeholder="https://api.openai.com"
              value={apiConfig.baseUrl || ''}
              onChange={e => updateConfig('baseUrl', e.target.value)}
            />
          </div>
          <div className="settings-item">
            <span className="settings-item-label">API Key</span>
            <div style={{display:'flex',alignItems:'center',gap:6,flex:1,minWidth:0,justifyContent:'flex-end'}}>
              <input
                className="settings-item-input"
                type={showKey ? 'text' : 'password'}
                placeholder="sk-..."
                value={apiConfig.apiKey || ''}
                onChange={e => updateConfig('apiKey', e.target.value)}
                style={{maxWidth:160}}
              />
              <button style={{background:'none',border:'none',cursor:'pointer',color:'var(--text-secondary)',padding:4}} onClick={() => setShowKey(!showKey)}>
                {showKey ? '🙈' : '👁'}
              </button>
            </div>
          </div>
          <div className="settings-item">
            <span className="settings-item-label">模型</span>
            <input
              className="settings-item-input"
              type="text"
              placeholder="gpt-4o-mini"
              value={apiConfig.model || ''}
              onChange={e => updateConfig('model', e.target.value)}
            />
          </div>
        </div>

        <div style={{padding:'12px 16px',display:'flex',gap:10,alignItems:'center'}}>
          <button
            className="btn btn-primary"
            onClick={handleTestApi}
            disabled={testing}
            style={{minWidth:100}}
          >
            {testing ? '测试中...' : '测试连接'}
          </button>
          {testResult && (
            <span style={{fontSize:13, color: testResult.ok ? '#4caf50' : '#f44336'}}>
              {testResult.ok ? '✓ ' : '✗ '}{testResult.msg}
            </span>
          )}
        </div>

        <div style={{padding:'0 16px 12px',fontSize:12,color:'var(--text-secondary)',lineHeight:1.6}}>
          支持所有 OpenAI 兼容接口（OpenAI、Groq、Together、本地 Ollama 等）。
          Anthropic 接口也支持（地址填 https://api.anthropic.com）。
        </div>
      </div>

      {/* Display */}
      <div className="settings-section">
        <div className="settings-section-title">显示设置</div>
        <div style={{background:'var(--surface)',borderTop:'1px solid var(--border)',borderBottom:'1px solid var(--border)'}}>
          <div className="settings-item">
            <span className="settings-item-label">阅读主题</span>
            <select
              className="settings-item-select"
              value={theme}
              onChange={e => setTheme(e.target.value)}
            >
              <option value="light">白色</option>
              <option value="sepia">米黄</option>
              <option value="dark">深色</option>
            </select>
          </div>
          <div className="settings-item">
            <span className="settings-item-label">字体大小</span>
            <div style={{display:'flex',alignItems:'center',gap:8}}>
              <button style={{width:28,height:28,borderRadius:6,border:'1px solid var(--border)',background:'var(--bg)',cursor:'pointer',fontSize:16,display:'flex',alignItems:'center',justifyContent:'center'}} onClick={() => setFontSize(Math.max(14, fontSize - 1))}>−</button>
              <span style={{fontSize:14,minWidth:28,textAlign:'center',fontWeight:600}}>{fontSize}</span>
              <button style={{width:28,height:28,borderRadius:6,border:'1px solid var(--border)',background:'var(--bg)',cursor:'pointer',fontSize:16,display:'flex',alignItems:'center',justifyContent:'center'}} onClick={() => setFontSize(Math.min(28, fontSize + 1))}>+</button>
            </div>
          </div>
        </div>
      </div>

      {/* About */}
      <div className="settings-section">
        <div className="settings-section-title">关于</div>
        <div style={{background:'var(--surface)',borderTop:'1px solid var(--border)',borderBottom:'1px solid var(--border)'}}>
          <div className="settings-item">
            <span className="settings-item-label">版本</span>
            <span className="settings-item-value">1.0.0</span>
          </div>
          <div className="settings-item">
            <span className="settings-item-label">功能</span>
            <span className="settings-item-value">EPUB / PDF / 网页阅读 + CEFR 标注</span>
          </div>
        </div>
        <div style={{padding:'12px 16px',fontSize:12,color:'var(--text-secondary)',lineHeight:1.6}}>
          所有翻译结果本地缓存，相同单词不会重复调用 API。
        </div>
      </div>
    </div>
  );
}
