const fs = require('fs');
const path = require('path');

const tsxPath = path.join(__dirname, 'ui', 'src', 'pages', 'AboutPage.tsx');
let tsxContent = fs.readFileSync(tsxPath, 'utf8');

const showcaseJSX = `
      <div className="about-grand-showcase">
        <div className="about-grand-showcase__aura"></div>
        <div className="about-grand-showcase__logo-container">
          <EasyToolsBolt size={140} className="about-grand-showcase__logo" />
        </div>
        <div className="about-grand-showcase__text">
          <h1 className="about-grand-showcase__title">EasyTools</h1>
          <p className="about-grand-showcase__subtitle">The Ultimate Windows Productivity Suite</p>
        </div>
      </div>
`;

// Insert after `<div className="about-page" style={{ animation: 'fadeIn 0.3s ease' }}>`
tsxContent = tsxContent.replace(
  /<div className="about-page"[^>]*>/,
  `$&
${showcaseJSX}`
);

// We can remove the small logo from the card now, since it's featured at the top!
// Actually, let's keep the card title clean.
tsxContent = tsxContent.replace(
  /<div className="about-hero__logo-box">[\s\S]*?<\/div>/,
  ''
);

fs.writeFileSync(tsxPath, tsxContent);

const cssPath = path.join(__dirname, 'ui', 'src', 'pages', 'AboutPage.css');
let cssContent = fs.readFileSync(cssPath, 'utf8');

const newCSS = `
/* Grand Showcase */
.about-grand-showcase {
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 40px 20px 30px;
  margin-bottom: 24px;
  border-radius: 16px;
  background: linear-gradient(145deg, #0f172a 0%, #1e293b 100%);
  overflow: hidden;
  box-shadow: 0 12px 32px rgba(0, 0, 0, 0.2), inset 0 1px 0 rgba(255,255,255,0.05);
  border: 1px solid rgba(255,255,255,0.05);
}

.about-grand-showcase::before {
  content: "";
  position: absolute;
  top: 0; left: 0; right: 0; bottom: 0;
  background-image: 
    radial-gradient(circle at 15% 50%, rgba(56, 189, 248, 0.08) 0%, transparent 50%),
    radial-gradient(circle at 85% 30%, rgba(124, 58, 237, 0.08) 0%, transparent 50%);
  pointer-events: none;
}

.about-grand-showcase__aura {
  position: absolute;
  width: 200px;
  height: 200px;
  background: rgba(56, 189, 248, 0.15);
  filter: blur(60px);
  border-radius: 50%;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -60%);
  pointer-events: none;
}

.about-grand-showcase__logo-container {
  position: relative;
  z-index: 2;
  margin-bottom: 20px;
  animation: floatLogo 6s ease-in-out infinite;
}

.about-grand-showcase__logo {
  filter: drop-shadow(0 20px 30px rgba(0, 0, 0, 0.4));
  border-radius: 32px; /* If it's a squircle image, we can just let it display naturally */
}

.about-grand-showcase__text {
  position: relative;
  z-index: 2;
  text-align: center;
}

.about-grand-showcase__title {
  margin: 0;
  font-size: 2.2rem;
  font-weight: 800;
  letter-spacing: -0.03em;
  color: #f8fafc;
  background: linear-gradient(to right, #f8fafc, #cbd5e1);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  line-height: 1.1;
}

.about-grand-showcase__subtitle {
  margin: 6px 0 0 0;
  font-size: 0.95rem;
  color: #94a3b8;
  font-weight: 500;
  letter-spacing: 0.05em;
  text-transform: uppercase;
}

@keyframes floatLogo {
  0% { transform: translateY(0px); }
  50% { transform: translateY(-8px); }
  100% { transform: translateY(0px); }
}
`;

cssContent = newCSS + "\n" + cssContent;
fs.writeFileSync(cssPath, cssContent);

console.log("Showcase injected!");
