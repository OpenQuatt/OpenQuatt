import { escapeHtml } from "../core/html.js";
import { t } from "../i18n/index.js";

const copy = (key) => escapeHtml(t(key));

export function renderLearningExplanation() {
  return `<section class="oq-helper-panel oq-learning-explanation">
    <header>
      <p class="oq-helper-section-copy">${copy("learningExplanation.intro")}</p>
    </header>

    <section class="oq-learning-split">
      <div>
        <h4>${copy("learningExplanation.measureHeading")}</h4>
        <dl>
          <div>
            <dt>${copy("learningExplanation.roomHeading")}</dt>
            <dd>${copy("learningExplanation.roomCopy")}</dd>
          </div>
          <div>
            <dt>${copy("learningExplanation.waterHeading")}</dt>
            <dd>${copy("learningExplanation.waterCopy")}</dd>
          </div>
          <div>
            <dt>${copy("learningExplanation.statusHeading")}</dt>
            <dd>${copy("learningExplanation.statusCopy")}</dd>
          </div>
        </dl>
      </div>
      <figure class="oq-learning-visual oq-learning-heat">
        <svg viewBox="0 0 490 240" role="img" aria-labelledby="oq-learning-sensors-title oq-learning-sensors-desc">
          <title id="oq-learning-sensors-title">${copy("learningExplanation.sensorsTitle")}</title>
          <desc id="oq-learning-sensors-desc">${copy("learningExplanation.sensorsDesc")}</desc>
          <defs>
            <path id="oq-learning-heat-supply" d="M141 176h13c12 0 15 16 27 16h154"/>
            <path id="oq-learning-heat-loss" d="M354 93c25-3 37-19 65-19h48"/>
            <path id="oq-learning-fan-blade" d="M72.8 181 C71.2 176.5 71.8 171.8 76.1 169.1 C78.7 167.4 82.3 167.3 83.7 169.7 C85.4 173 80.2 178.4 75.4 181.3 Z"/>
          </defs>
          <path d="M25 222h445" stroke="var(--oq-helper-line)" stroke-width="1.5"/>
          <path d="M187 105 297 32l109 73v116H187Z" fill="var(--oq-helper-card)"/>
          <path d="M187 212h209V103l10 7v111H187Z" fill="var(--oq-helper-card-muted)"/>
          <path d="M187 106v115h219V106" fill="none" stroke="var(--oq-helper-soft)" stroke-width="1.8" stroke-linejoin="round"/>
          <path d="M197 117 297 50l99 67v95H187" fill="none" stroke="var(--oq-helper-line)" stroke-width="1.5" stroke-linejoin="round"/>
          <path d="M172 110 297 26l125 84-2.5 3.7L297 31.5l-122.5 82.2Z" fill="var(--oq-helper-soft)"/>
          <path d="M178 114 297 34.5 416 114" fill="none" stroke="var(--oq-helper-muted)" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"/>
          <g stroke="var(--oq-helper-muted)" stroke-opacity=".33" stroke-width="1.25" stroke-linejoin="round">
            <rect x="356" y="121" width="32" height="27" rx="1.5" fill="var(--oq-helper-card-muted)"/>
            <path d="M372 121v27M356 134.5h32m-35 16.5h38" fill="none" stroke-linecap="round"/>
          </g>
          <path d="M53 215v7m76-7v7" stroke="var(--oq-helper-soft)" stroke-width="2"/>
          <rect x="41" y="150" width="100" height="65" rx="8" fill="var(--oq-helper-card)" stroke="var(--oq-helper-soft)" stroke-width="2"/>
          <rect x="101" y="161" width="31" height="42" rx="4" fill="var(--oq-helper-card-muted)" stroke="var(--oq-helper-line)"/>
          <circle cx="74" cy="182" r="20" fill="var(--oq-helper-card-muted)" stroke="var(--oq-helper-muted)" stroke-width="1.5"/>
          <g class="oq-learning-rotor" fill="var(--oq-helper-accent-deep)">
            <use href="#oq-learning-fan-blade"/>
            <use href="#oq-learning-fan-blade" transform="rotate(120 74 182)"/>
            <use href="#oq-learning-fan-blade" transform="rotate(240 74 182)"/>
          </g>
          <circle cx="74" cy="182" r="2.5" fill="var(--oq-helper-soft)"/>
          <path d="M109 169h15m-15 6h15m-15 7h15m-15 7h15m-15 6h15" stroke="var(--oq-helper-muted)" stroke-width="1.5" stroke-linecap="round"/>
          <use href="#oq-learning-heat-supply" class="warm-line" opacity=".4"/>
          <use href="#oq-learning-heat-supply" class="warm-line oq-learning-flow"/>
          <path d="M141 189h13c12 0 15 16 27 16h163a6.5 6.5 0 0 0 0-13h-9" fill="none" stroke="var(--oq-helper-muted)" stroke-width="2"/>
          <path d="m310 187 10 5-10 5Z" fill="var(--oq-helper-accent-deep)"/>
          <circle cx="219" cy="192" r="4.5" fill="var(--oq-helper-card)" stroke="var(--oq-helper-soft)" stroke-width="1.5"/>
          <text x="219" y="175" text-anchor="middle" class="picture-label">${copy("learningExplanation.flowLabel")}</text>
          <text x="342" y="175" text-anchor="middle" class="picture-label oq-learning-heat-input">${copy("learningExplanation.heatInputLabel")}</text>
          <g fill="var(--oq-helper-card)" stroke="var(--oq-helper-muted)" stroke-width="1.75" stroke-linecap="round">
            <path d="M231 117v-21a4 4 0 0 1 8 0v21a7 7 0 1 1-8 0Z"/>
            <path d="M235 107v16"/>
            <path d="M61 88V67a4 4 0 0 1 8 0v21a7 7 0 1 1-8 0Z"/>
            <path d="M65 78v16"/>
          </g>
          <text x="251" y="115" class="picture-label">${copy("learningExplanation.insideLabel")} · 20 °C</text>
          <text x="82" y="86" class="picture-label">${copy("learningExplanation.outsideLabel")} · 4 °C</text>
          <use href="#oq-learning-heat-loss" fill="none" stroke="var(--oq-helper-card-muted)" stroke-width="4.6" stroke-linecap="round"/>
          <use href="#oq-learning-heat-loss" class="cool-line oq-learning-flow"/>
          <path d="m465 68 12 6-12 6Z" fill="var(--oq-learning-cool)"/>
          <text x="475" y="57" text-anchor="end" class="picture-label oq-learning-heat-loss">${copy("learningExplanation.heatLossLabel")}</text>
          <path d="M130 132h19v44" class="picture-line"/>
          <g fill="var(--oq-helper-card)" stroke="var(--oq-helper-soft)" stroke-width="1.5">
            <circle cx="149" cy="176" r="3.5"/>
            <circle cx="149" cy="189" r="3.5"/>
          </g>
          <text x="39" y="137" class="picture-label">${copy("learningExplanation.waterLabel")}</text>
        </svg>
        <figcaption>${copy("learningExplanation.heatCaption")}</figcaption>
      </figure>
    </section>

    <section>
      <h4>${copy("learningExplanation.learnHeading")}</h4>
      <div class="oq-learning-pair">
        <article>
          <p class="oq-learning-kicker">${copy("learningExplanation.dayPeriod")}</p>
          <h5>${copy("learningExplanation.dayHeading")}</h5>
          <p>${copy("learningExplanation.dayCopy")}</p>
          <figure class="oq-learning-visual">
            <svg viewBox="0 0 380 158" role="img" aria-labelledby="oq-learning-line-picture-title oq-learning-line-picture-desc">
              <title id="oq-learning-line-picture-title">${copy("learningExplanation.lineTitle")}</title>
              <desc id="oq-learning-line-picture-desc">${copy("learningExplanation.lineDesc")}</desc>
              <path d="M37 39h314M37 68h314M37 97h314" class="plot-grid"/>
              <path d="M37 30v91h314" class="plot-axis"/>
              <path d="M52 39 341 109" fill="none" stroke="var(--oq-learning-green)" stroke-width="3" stroke-linecap="round"/>
              <g fill="var(--oq-helper-accent-deep)" stroke="var(--oq-helper-card)" stroke-width="1.8">
              <circle cx="75" cy="51" r="5"/><circle cx="101" cy="46" r="5"/><circle cx="134" cy="60" r="5"/>
              <circle cx="175" cy="76" r="5"/><circle cx="216" cy="74" r="5"/><circle cx="254" cy="94" r="5"/>
              <circle cx="288" cy="92" r="5"/><circle cx="324" cy="105" r="5"/>
              </g>
              <text x="36" y="19" class="plot-label">${copy("learningExplanation.heatDemandLabel")}</text>
              <text x="351" y="144" text-anchor="end" class="plot-label">${copy("learningExplanation.outsideTemperatureLabel")}</text>
            </svg>
            <figcaption>
              <span><i class="oq-learning-key oq-learning-key--dot"></i>${copy("learningExplanation.dayPoints")}</span>
              <span><i class="oq-learning-key oq-learning-key--green"></i>${copy("learningExplanation.learnedLine")}</span>
            </figcaption>
          </figure>
        </article>
        <article>
          <p class="oq-learning-kicker">${copy("learningExplanation.responsePeriod")}</p>
          <h5>${copy("learningExplanation.responseHeading")}</h5>
          <p>${copy("learningExplanation.responseCopy")}</p>
          <figure class="oq-learning-visual">
            <svg viewBox="0 0 380 158" role="img" aria-labelledby="oq-learning-response-picture-title oq-learning-response-picture-desc">
              <title id="oq-learning-response-picture-title">${copy("learningExplanation.responseTitle")}</title>
              <desc id="oq-learning-response-picture-desc">${copy("learningExplanation.responseDesc")}</desc>
              <path d="M37 39h314M37 68h314M37 97h314" class="plot-grid"/>
              <path d="M37 30v91h314" class="plot-axis"/>
              <path d="M37 104C135 101 176 78 232 65S308 52 351 46V121H37Z" fill="var(--oq-helper-accent-soft)"/>
              <path d="M37 104C135 101 176 78 232 65S308 52 351 46" class="warm-line"/>
              <circle cx="37" cy="104" r="4.5" fill="var(--oq-helper-accent-deep)"/>
              <circle cx="351" cy="46" r="4.5" fill="var(--oq-helper-accent-deep)"/>
              <text x="37" y="19" class="plot-label">${copy("learningExplanation.roomTemperatureLabel")}</text>
              <text x="37" y="144" class="plot-label">${copy("learningExplanation.startLabel")}</text>
              <text x="351" y="144" text-anchor="end" class="plot-label">${copy("learningExplanation.after30Label")}</text>
            </svg>
            <figcaption>
              <span><i class="oq-learning-key"></i>${copy("learningExplanation.warmingLine")}</span>
            </figcaption>
          </figure>
        </article>
      </div>
      <p>${copy("learningExplanation.reviewCopy")}</p>
    </section>

    <section class="oq-learning-split">
      <div>
        <h4>${copy("learningExplanation.forecastHeading")}</h4>
        <p>${copy("learningExplanation.forecastCopy")}</p>
        <p>${copy("learningExplanation.scenarioCopy")}</p>
        <p>${copy("learningExplanation.pauseCopy")}</p>
      </div>
      <figure class="oq-learning-visual">
        <svg viewBox="0 0 460 222" role="img" aria-labelledby="oq-learning-forecast-picture-title oq-learning-forecast-picture-desc">
          <title id="oq-learning-forecast-picture-title">${copy("learningExplanation.forecastTitle")}</title>
          <desc id="oq-learning-forecast-picture-desc">${copy("learningExplanation.forecastDesc")}</desc>
          <path d="M47 49h370M47 82h370M47 115h370" class="plot-grid"/>
          <path d="M47 42v116h370" class="plot-axis"/>
          <path d="M108 90v68m62-62v62m247-30v30" fill="none" stroke="var(--oq-learning-heat-tint)" stroke-width="1.5" stroke-dasharray="3 5"/>
          <path d="M47 82Q79 87 108 90T170 96Q295 109 417 128" fill="none" stroke="var(--oq-helper-accent-deep)" stroke-width="3" stroke-dasharray="7 7" stroke-linecap="round"/>
          <circle cx="47" cy="82" r="5.5" fill="var(--oq-helper-soft)"/>
          <g fill="var(--oq-helper-card)" stroke="var(--oq-helper-accent-deep)" stroke-width="2.5">
          <circle cx="108" cy="90" r="5"/><circle cx="170" cy="96" r="5"/><circle cx="417" cy="128" r="5"/>
          </g>
          <text x="47" y="29" class="plot-label">${copy("learningExplanation.roomTemperatureLabel")}</text>
          <text x="47" y="180" text-anchor="middle" class="plot-label">${copy("learningExplanation.nowLabel")}</text>
          <text x="108" y="180" text-anchor="middle" class="plot-label">${copy("learningExplanation.min30Label")}</text>
          <text x="170" y="180" text-anchor="middle" class="plot-label">${copy("learningExplanation.hour1Label")}</text>
          <text x="417" y="180" text-anchor="middle" class="plot-label">${copy("learningExplanation.hour3Label")}</text>
          <path d="M145 205h30" stroke="var(--oq-helper-accent-deep)" stroke-width="2.5" stroke-dasharray="6 5"/>
          <text x="183" y="210" class="plot-label">${copy("learningExplanation.scenarioLabel")}</text>
        </svg>
      </figure>
    </section>
    <p class="oq-learning-caption">${copy("learningExplanation.caption")}</p>
  </section>`;
}
