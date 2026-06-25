const revealTargets = document.querySelectorAll(
  ".section, .scenario-card, .mini-card, .timeline-item, .network-card, .milestone-card, .cta-panel"
);

for (const item of revealTargets) {
  item.classList.add("reveal");
}

const observer = new IntersectionObserver(
  entries => {
    for (const entry of entries) {
      if (entry.isIntersecting) {
        entry.target.classList.add("is-visible");
        observer.unobserve(entry.target);
      }
    }
  },
  {
    threshold: 0.14,
    rootMargin: "0px 0px -40px 0px"
  }
);

for (const item of revealTargets) {
  observer.observe(item);
}

const timelineItems = Array.from(document.querySelectorAll(".timeline-item"));
let activeIndex = 0;

function cycleTimeline() {
  for (const [index, item] of timelineItems.entries()) {
    item.style.outline = index === activeIndex ? "1px solid rgba(126, 242, 255, 0.45)" : "1px solid transparent";
    item.style.transform = index === activeIndex ? "translateY(-4px)" : "translateY(0)";
  }
  activeIndex = (activeIndex + 1) % timelineItems.length;
}

if (timelineItems.length > 0 && !window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
  cycleTimeline();
  window.setInterval(cycleTimeline, 2200);
}
