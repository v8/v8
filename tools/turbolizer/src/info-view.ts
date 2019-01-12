import { View } from "./view";

export class InfoView extends View {

  constructor(idOrContainer: HTMLElement | string) {
    super(idOrContainer);
  }

  initializeContent(data: any, rememberedSelection: Set<any>): void {
    fetch("/info-view.html")
      .then(response => response.text())
      .then(htmlText => this.divNode.innerHTML = htmlText);
  }

  createViewElement(): HTMLElement {
    const infoContainer = document.createElement("div");
    infoContainer.classList.add("info-container");
    return infoContainer;
  }

  deleteContent(): void {
    this.divNode.innerHTML = "";
  }

  detachSelection(): Set<string> {
    return null;
  }
}
