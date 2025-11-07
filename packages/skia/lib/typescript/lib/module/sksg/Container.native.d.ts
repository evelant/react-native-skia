export function createContainer(Skia: any, nativeId: any): StaticContainer | NativeReanimatedContainer;
import { StaticContainer } from "./StaticContainer";
declare class NativeReanimatedContainer extends Container {
    constructor(Skia: any, nativeId: any);
    nativeId: any;
    recorderA: ReanimatedRecorder;
    recorderB: ReanimatedRecorder;
    currentRecorder: ReanimatedRecorder;
    picture: any;
    redraw(): void;
    mapperId: any;
}
import { Container } from "./StaticContainer";
import { ReanimatedRecorder } from "./Recorder/ReanimatedRecorder";
export {};
