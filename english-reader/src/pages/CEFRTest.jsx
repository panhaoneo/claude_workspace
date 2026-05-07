import React, { useState } from 'react';
import { TEST_WORDS, LEVEL_INFO, LEVELS } from '../utils/cefrWords.js';
import { useApp } from '../App.jsx';

const ALL_TEST_WORDS = LEVELS.flatMap(level =>
  TEST_WORDS[level]?.map(w => ({ ...w, level })) || []
);

export default function CEFRTest() {
  const { cefrLevel, setCefrLevel } = useApp();
  const [phase, setPhase] = useState('intro'); // intro | testing | result
  const [index, setIndex] = useState(0);
  const [answers, setAnswers] = useState({}); // word -> true/false
  const [result, setResult] = useState(null);

  function startTest() {
    setIndex(0);
    setAnswers({});
    setResult(null);
    setPhase('testing');
  }

  function answer(known) {
    const word = ALL_TEST_WORDS[index].word;
    const newAnswers = { ...answers, [word]: known };
    setAnswers(newAnswers);

    if (index + 1 >= ALL_TEST_WORDS.length) {
      const level = calculateLevel(newAnswers);
      setResult(level);
      setCefrLevel(level);
      setPhase('result');
    } else {
      setIndex(index + 1);
    }
  }

  function calculateLevel(ans) {
    // For each CEFR level, compute how many words the user knows
    const scoreByLevel = {};
    for (const level of LEVELS) {
      const words = TEST_WORDS[level] || [];
      const known = words.filter(w => ans[w.word] === true).length;
      scoreByLevel[level] = known / words.length;
    }

    // Find the highest level where user knows >60% of words
    let bestLevel = 'A1';
    for (const level of LEVELS) {
      if (scoreByLevel[level] >= 0.6) {
        bestLevel = level;
      } else {
        break;
      }
    }
    return bestLevel;
  }

  const current = ALL_TEST_WORDS[index];
  const progress = (index / ALL_TEST_WORDS.length) * 100;
  const info = result ? LEVEL_INFO[result] : LEVEL_INFO[cefrLevel];

  if (phase === 'intro') {
    return (
      <div className="page">
        <div className="cefr-page">
          <h1>CEFR 等级测试</h1>
          <p className="subtitle">通过 30 个单词评估你的英语水平</p>

          <div style={{background:'var(--surface)',borderRadius:16,padding:20,marginBottom:24,boxShadow:'0 2px 8px rgba(0,0,0,0.06)'}}>
            <div style={{fontSize:13,color:'var(--text-secondary)',marginBottom:8}}>当前设置的等级</div>
            <div style={{display:'flex',alignItems:'center',gap:12}}>
              <div className="cefr-level-badge" style={{background: info?.color}}>
                {cefrLevel}
              </div>
              <div>
                <div style={{fontWeight:600,fontSize:15}}>{info?.label}</div>
                <div style={{fontSize:13,color:'var(--text-secondary)'}}>{info?.en}</div>
              </div>
            </div>
            <p className="cefr-description" style={{marginTop:12,marginBottom:0}}>{info?.desc}</p>
          </div>

          <div style={{marginBottom:24}}>
            <div style={{fontWeight:600,marginBottom:12}}>直接选择等级</div>
            <div className="level-chips">
              {LEVELS.map(l => (
                <button
                  key={l}
                  className={`level-chip ${cefrLevel === l ? 'active' : ''}`}
                  style={cefrLevel === l ? {background: LEVEL_INFO[l].color, borderColor: LEVEL_INFO[l].color} : {}}
                  onClick={() => setCefrLevel(l)}
                >
                  {l}
                </button>
              ))}
            </div>
            <p style={{fontSize:13,color:'var(--text-secondary)',marginTop:8}}>
              阅读器将标注超过你所选等级的单词
            </p>
          </div>

          <button className="btn btn-primary" style={{width:'100%',justifyContent:'center',padding:14,fontSize:16}} onClick={startTest}>
            开始测试 (30题)
          </button>

          <div style={{marginTop:24}}>
            <div style={{fontWeight:600,marginBottom:12}}>各级别说明</div>
            {LEVELS.map(l => (
              <div key={l} style={{display:'flex',gap:12,padding:'10px 0',borderBottom:'1px solid var(--border)'}}>
                <div style={{width:36,height:36,borderRadius:8,background:LEVEL_INFO[l].color,color:'#fff',display:'flex',alignItems:'center',justifyContent:'center',fontWeight:700,fontSize:14,flexShrink:0}}>
                  {l}
                </div>
                <div>
                  <div style={{fontWeight:600,fontSize:14}}>{LEVEL_INFO[l].label}</div>
                  <div style={{fontSize:12,color:'var(--text-secondary)',marginTop:2}}>{LEVEL_INFO[l].desc}</div>
                </div>
              </div>
            ))}
          </div>
        </div>
      </div>
    );
  }

  if (phase === 'testing') {
    return (
      <div className="page">
        <div className="cefr-page">
          <div style={{display:'flex',justifyContent:'space-between',alignItems:'center',marginBottom:12}}>
            <span style={{fontSize:14,color:'var(--text-secondary)'}}>
              {index + 1} / {ALL_TEST_WORDS.length}
            </span>
            <span style={{fontSize:12,color:'var(--text-secondary)',background:'var(--border)',padding:'2px 8px',borderRadius:8,fontWeight:600}}>
              {current.level}
            </span>
          </div>

          <div className="test-progress-bar">
            <div className="test-progress-fill" style={{width: `${progress}%`}} />
          </div>

          <div className="test-word-card">
            <div className="test-word">{current.word}</div>
            <div style={{fontSize:13,color:'var(--text-secondary)',marginTop:6}}>{current.hint}</div>
          </div>

          <p style={{textAlign:'center',fontSize:14,color:'var(--text-secondary)',marginBottom:16}}>
            你认识这个单词吗？
          </p>

          <div className="test-buttons">
            <button className="test-btn dont-know" onClick={() => answer(false)}>
              ✕ 不认识
            </button>
            <button className="test-btn know" onClick={() => answer(true)}>
              ✓ 认识
            </button>
          </div>
        </div>
      </div>
    );
  }

  // Result phase
  const resultInfo = LEVEL_INFO[result];
  return (
    <div className="page">
      <div className="cefr-page">
        <h1>测试结果</h1>
        <p className="subtitle">根据你的答题情况，你的英语水平为</p>

        <div className="test-result">
          <div style={{fontSize:16,color:'var(--text-secondary)'}}>你的 CEFR 等级</div>
          <div className="result-level" style={{color: resultInfo.color}}>{result}</div>
          <div style={{fontSize:18,fontWeight:700,marginBottom:8}}>{resultInfo.label}</div>
          <div style={{fontSize:14,color:'var(--text-secondary)',lineHeight:1.6}}>{resultInfo.desc}</div>
        </div>

        <div style={{marginTop:20,background:'var(--surface)',borderRadius:16,padding:16,boxShadow:'0 2px 8px rgba(0,0,0,0.06)'}}>
          <div style={{fontWeight:600,marginBottom:10}}>各级别得分</div>
          {LEVELS.map(level => {
            const words = TEST_WORDS[level] || [];
            const known = words.filter(w => answers[w.word] === true).length;
            const pct = Math.round(known / words.length * 100);
            return (
              <div key={level} style={{display:'flex',alignItems:'center',gap:10,marginBottom:8}}>
                <span style={{width:28,fontSize:13,fontWeight:700,color:LEVEL_INFO[level].color}}>{level}</span>
                <div style={{flex:1,height:8,background:'var(--border)',borderRadius:4,overflow:'hidden'}}>
                  <div style={{height:'100%',width:`${pct}%`,background:LEVEL_INFO[level].color,borderRadius:4,transition:'width 0.5s'}} />
                </div>
                <span style={{fontSize:12,color:'var(--text-secondary)',width:40,textAlign:'right'}}>{known}/{words.length}</span>
              </div>
            );
          })}
        </div>

        <div style={{marginTop:16,padding:12,background:'#e3f2fd',borderRadius:12,fontSize:14,color:'#1565c0'}}>
          阅读器已设置为标注超过 <strong>{result}</strong> 等级的单词
        </div>

        <button className="btn btn-primary" style={{width:'100%',justifyContent:'center',padding:14,marginTop:16}} onClick={() => setPhase('intro')}>
          完成
        </button>
      </div>
    </div>
  );
}
