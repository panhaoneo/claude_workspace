// CEFR level index for ordering
export const LEVELS = ['A1', 'A2', 'B1', 'B2', 'C1', 'C2'];

export function getLevelIndex(level) {
  return LEVELS.indexOf(level);
}

export function isWordAboveLevel(wordLevel, userLevel) {
  if (!wordLevel) return true; // unknown word
  return getLevelIndex(wordLevel) > getLevelIndex(userLevel);
}

// Common A1+A2 words — skip API for these
// Sources: Oxford 5000, frequency analysis
const A1_WORDS = new Set([
  // Articles, pronouns, prepositions, conjunctions
  'a','an','the','i','me','my','we','us','our','you','your','he','him','his','she','her',
  'it','its','they','them','their','this','that','these','those','who','what','which',
  'where','when','why','how','and','but','or','so','if','as','at','by','for','from',
  'in','of','off','on','out','to','up','with','not','no','nor','yet',
  // Core verbs
  'be','am','is','are','was','were','been','being','do','does','did','done','doing',
  'have','has','had','having','will','would','can','could','shall','should','may',
  'might','must','get','got','go','goes','went','gone','come','came','make','made',
  'take','took','taken','know','knew','known','think','thought','see','saw','seen',
  'say','said','look','looked','use','used','find','found','give','gave','given',
  'tell','told','ask','asked','seem','seemed','feel','felt','try','tried','leave',
  'left','call','called','keep','kept','let','put','mean','meant','become','became',
  'show','showed','shown','hear','heard','play','played','run','ran','move','moved',
  'live','lived','read','write','wrote','written','eat','ate','eaten','drink','drank',
  'drunk','open','close','sit','sat','stand','stood','walk','sleep','slept','start',
  'stop','help','love','like','want','need','turn','work','learn','buy','bought',
  'sell','sold','bring','brought','begin','began','begun','break','broke','broken',
  'catch','caught','choose','chose','chosen','fall','fell','fallen','hold','held',
  'meet','met','pay','paid','send','sent','spend','spent','speak','spoke','spoken',
  'win','won','build','built','forget','forgot','forgotten','grow','grew','grown',
  'lose','lost','ride','rode','ridden','sing','sang','sung','swim','swam','swum',
  'teach','taught','throw','threw','thrown','wear','wore','worn','cut','hit','hurt',
  // Common nouns
  'cat','dog','man','woman','boy','girl','child','children','baby','family','friend',
  'people','person','house','home','school','book','car','road','street','water',
  'food','day','time','year','night','morning','afternoon','evening','week','month',
  'world','country','city','town','place','room','door','window','table','chair',
  'bed','floor','wall','name','word','number','part','hand','head','eye','face',
  'arm','leg','foot','heart','life','body','phone','money','way','thing','work',
  'job','game','team','group','class','question','answer','story','picture','color',
  'colour','line','side','air','fire','water','earth','tree','flower','animal','bird',
  'fish','horse','sun','moon','sky','sea','river','mountain','park','garden','city',
  'town','market','shop','store','hospital','hotel','airport','station','bank',
  // Adjectives
  'big','small','little','large','great','good','bad','new','old','young','long',
  'short','high','low','hot','cold','warm','cool','fast','slow','hard','soft',
  'easy','difficult','right','wrong','true','false','same','different','full','empty',
  'open','closed','happy','sad','angry','afraid','ready','free','clean','dirty',
  'quiet','loud','strong','weak','rich','poor','safe','dangerous','beautiful','ugly',
  'nice','fine','okay','ok','sure','real','dark','light','heavy','light','bright',
  'white','black','red','blue','green','yellow','orange','pink','brown','grey','gray',
  // Numbers and quantities
  'zero','one','two','three','four','five','six','seven','eight','nine','ten',
  'hundred','thousand','million','first','second','third','last','next','other',
  'all','both','each','every','any','some','many','much','more','most','few',
  'less','little','enough','half','only','just','very','quite','really','too',
  // Time and place
  'here','there','now','then','today','tomorrow','yesterday','soon','still','again',
  'always','never','often','sometimes','usually','already','yet','well','also',
  'too','back','away','up','down','left','right','inside','outside','near','far',
  'above','below','before','after','behind','front','between','among','through',
  'around','along','across'
]);

const A2_WORDS = new Set([
  // Communication & social
  'hello','goodbye','please','thank','sorry','excuse','welcome','congratulations',
  'agree','disagree','explain','describe','promise','suggest','offer','invite','refuse',
  'introduce','reply','respond','message','letter','email','phone','call','meeting',
  // Travel & places
  'travel','visit','arrive','leave','fly','drive','trip','journey','tour','holiday',
  'vacation','hotel','restaurant','station','airport','museum','theatre','theater',
  'cinema','library','hospital','office','factory','church','castle','beach','forest',
  // Daily life
  'breakfast','lunch','dinner','meal','cook','recipe','ingredient','supermarket',
  'shopping','price','cost','pay','bill','ticket','passport','bag','luggage','clothes',
  'shirt','trousers','shoes','dress','hat','coat','wash','clean','tidy','fix','repair',
  // Work & study
  'job','career','boss','colleague','employee','customer','client','business','company',
  'subject','lesson','homework','exam','test','grade','pass','fail','study','practice',
  'exercise','project','report','presentation','meeting','interview','experience',
  // Nature & environment
  'weather','rain','snow','wind','sunny','cloudy','storm','temperature','nature',
  'environment','pollution','energy','electricity','planet','climate','season',
  'spring','summer','autumn','winter','forest','mountain','ocean','island','desert',
  // Health
  'health','doctor','hospital','medicine','sick','ill','pain','hurt','injury','sleep',
  'tired','exercise','diet','weight','stress','headache','fever','cough','cold','flu',
  // Entertainment
  'music','song','dance','sport','team','player','game','match','competition',
  'television','tv','movie','film','actor','actress','news','magazine','internet',
  'computer','phone','technology','app','website','social','media','photo','camera',
  // Feelings & personality
  'happy','sad','angry','excited','surprised','worried','nervous','bored','interested',
  'friendly','kind','helpful','honest','clever','intelligent','funny','serious','polite',
  'important','interesting','exciting','wonderful','terrible','awful','boring',
  'strange','popular','famous','typical','perfect','normal','usual','special',
  // Other common words
  'possible','probably','maybe','nearly','almost','enough','quite','especially',
  'together','suddenly','quickly','slowly','carefully','easily','actually','really',
  'basically','generally','simply','directly','recently','already','finally','however',
  'although','because','since','while','unless','until','whether','except','instead',
  'despite','during','behind','between','among','across','through','against','without',
  'according','example','idea','reason','information','opinion','fact','situation',
  'problem','solution','decision','choice','plan','change','result','effect','event',
  'picture','image','sound','voice','number','amount','level','size','type','kind',
  'area','region','community','society','culture','government','politics','economy',
  'history','language','science','technology','art','music','sport','education',
  'future','past','present','century','period','moment','chance','opportunity',
  'relationship','communication','difference','similarity','advantage','disadvantage'
]);

export function getWordLevel(word) {
  const w = word.toLowerCase().replace(/[^a-z]/g, '');
  if (A1_WORDS.has(w)) return 'A1';
  if (A2_WORDS.has(w)) return 'A2';
  return null; // unknown — needs API check
}

export function isBasicWord(word) {
  const w = word.toLowerCase().replace(/[^a-z]/g, '');
  return A1_WORDS.has(w) || A2_WORDS.has(w);
}

// Words used in CEFR level test — 5 words per level
export const TEST_WORDS = {
  A1: [
    { word: 'cat', hint: '一种常见宠物' },
    { word: 'run', hint: '一种移动方式' },
    { word: 'house', hint: '居住的地方' },
    { word: 'eat', hint: '进食的动作' },
    { word: 'big', hint: '描述尺寸' }
  ],
  A2: [
    { word: 'travel', hint: '去不同地方' },
    { word: 'weather', hint: '自然现象' },
    { word: 'invite', hint: '社交动作' },
    { word: 'collect', hint: '收集物品' },
    { word: 'subject', hint: '学习内容' }
  ],
  B1: [
    { word: 'negotiate', hint: '商业/外交用词' },
    { word: 'evidence', hint: '法律/科学用词' },
    { word: 'colleague', hint: '工作场合' },
    { word: 'appropriate', hint: '描述合适程度' },
    { word: 'flexible', hint: '描述适应能力' }
  ],
  B2: [
    { word: 'controversial', hint: '引发争议的' },
    { word: 'phenomenon', hint: '描述现象' },
    { word: 'constitute', hint: '构成/组成' },
    { word: 'acknowledge', hint: '承认/认可' },
    { word: 'sophisticated', hint: '复杂精密的' }
  ],
  C1: [
    { word: 'elusive', hint: '难以捉摸的' },
    { word: 'pragmatic', hint: '务实的' },
    { word: 'nuanced', hint: '细致入微的' },
    { word: 'pervasive', hint: '普遍存在的' },
    { word: 'articulate', hint: '清晰表达的' }
  ],
  C2: [
    { word: 'ephemeral', hint: '短暂的' },
    { word: 'laconic', hint: '简洁寡言的' },
    { word: 'magnanimous', hint: '宽宏大量的' },
    { word: 'sycophantic', hint: '阿谀奉承的' },
    { word: 'perspicacious', hint: '敏锐的' }
  ]
};

// CEFR level descriptions
export const LEVEL_INFO = {
  A1: {
    label: 'A1 入门级',
    en: 'Beginner',
    desc: '能理解和使用熟悉的日常表达，与他人进行简单互动。',
    color: '#4caf50'
  },
  A2: {
    label: 'A2 初级',
    en: 'Elementary',
    desc: '能理解常用句子和表达，能描述基本个人信息、日常生活。',
    color: '#8bc34a'
  },
  B1: {
    label: 'B1 中级',
    en: 'Intermediate',
    desc: '能理解工作、学习中常见主题，能描述经历、梦想和希望。',
    color: '#2196f3'
  },
  B2: {
    label: 'B2 中高级',
    en: 'Upper Intermediate',
    desc: '能理解复杂文章的主旨，能与母语者流利交流。',
    color: '#ff9800'
  },
  C1: {
    label: 'C1 高级',
    en: 'Advanced',
    desc: '能理解各种长篇、难度较大的文章，能流利自如地表达。',
    color: '#f44336'
  },
  C2: {
    label: 'C2 精通级',
    en: 'Proficiency',
    desc: '能毫不费力地理解听到或读到的内容，达到近母语水平。',
    color: '#9c27b0'
  }
};
