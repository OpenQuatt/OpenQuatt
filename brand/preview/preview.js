const themeButton = document.querySelector(".theme-button");
const sizeInput = document.querySelector("#mark-size");
const liveMark = document.querySelector("#live-mark");
const sizeOutput = document.querySelector('output[for="mark-size"]');

function updateLiveMark() {
  const size = Number(sizeInput.value);
  const useMicro = size < 21;
  const surface = document.body.classList.contains("light-board") ? "light" : "dark";
  liveMark.src = useMicro
    ? `../logos/svg/openquatt-symbol-micro-${surface}.svg`
    : `../logos/svg/openquatt-symbol-${surface}.svg`;
  liveMark.width = size;
  liveMark.height = size;
  sizeOutput.value = `${size} px${useMicro ? " · micro" : ""}`;
}

themeButton.addEventListener("click", () => {
  const isLight = document.body.classList.toggle("light-board");
  themeButton.setAttribute("aria-pressed", String(isLight));
  themeButton.textContent = isLight ? "Dark board" : "Light board";
  updateLiveMark();
});

sizeInput.addEventListener("input", updateLiveMark);

const observer = new IntersectionObserver(
  (entries) => {
    for (const entry of entries) {
      if (!entry.isIntersecting) continue;
      entry.target.classList.add("is-visible");
      observer.unobserve(entry.target);
    }
  },
  { threshold: 0.12 },
);

for (const element of document.querySelectorAll(".reveal")) observer.observe(element);
