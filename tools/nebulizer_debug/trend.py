from array import array
from collections.abc import Sequence
from PyQt6.QtCore import Qt, QRectF, QPointF
from PyQt6.QtGui import QColor, QPainter, QPainterPath, QPen
from PyQt6.QtWidgets import QWidget

LABELS = {'PB11':'PB11 出口温度','PB10':'PB10 旧反馈温度','PB12':'PB12 保护温度','PA5':'PA5 锅体温度'}

SERIES = {
    '温度 °C': [('PB11','#24b8a5'),('PB10','#e8b34c'),('PB12','#e16a83'),('PA5','#8b94ec'),('目标','#c2cbd4')],
    '输出 %': [('加热','#ff914d'),('风机','#64b5f6'),('积分','#b48be8')],
    '误差 °C': [('加热误差','#f062c0'),('风机误差','#a9cf54')],
}

SERIES = {'综合曲线': [item for items in SERIES.values() for item in items]}
UNITS = {name: '%' if name in ('加热','风机','积分') else '°C' for name,_ in SERIES['综合曲线']}

class SampleBuffer(Sequence):
    """Compact, dynamically grown ring; avoids millions of Python dictionaries."""
    def __init__(self,maxlen=864000):
        self.maxlen=maxlen
        self.keys=None
        self.data=array('d')
        self.start=0
        self.count=0
    def __len__(self):return self.count
    def __getitem__(self,index):
        if index<0:index+=self.count
        if not 0<=index<self.count:raise IndexError(index)
        base=((self.start+index)%self.maxlen)*len(self.keys)
        return dict(zip(self.keys,self.data[base:base+len(self.keys)]))
    def append(self,sample):
        if self.keys is None:self.keys=tuple(sample)
        values=array('d',(sample[k] for k in self.keys))
        if self.count<self.maxlen:
            self.data.extend(values);self.count+=1
        else:
            base=self.start*len(self.keys)
            self.data[base:base+len(self.keys)]=values
            self.start=(self.start+1)%self.maxlen
    def clear(self):
        self.data=array('d');self.start=self.count=0
    def time_at(self,index):
        return self.data[((self.start+index)%self.maxlen)*len(self.keys)]
    def bound(self,value):
        lo,hi=0,self.count
        while lo<hi:
            mid=(lo+hi)//2
            if self.time_at(mid)<value:lo=mid+1
            else:hi=mid
        return lo
    def visible(self,left,right,limit=2000):
        first=self.bound(left);last=self.bound(right)
        if last<self.count and self.time_at(last)<=right:last+=1
        count=last-first
        if count<=0:return []
        if count<=limit:return [self[i] for i in range(first,last)]
        return [self[first+i*(count-1)//(limit-1)] for i in range(limit)]

class Trend(QWidget):
    def __init__(self):
        super().__init__()
        self.samples=SampleBuffer()
        self.group='综合曲线'
        self.hidden=set()
        self.span=120.
        self.offset=0.
        self.drag=None
        self.y_ranges={}
        self.last_range=(0.,100.)
        self.setMinimumHeight(300)

    def add(self,snap):
        r=snap['runtime']
        if self.samples and self.samples[-1]['sample_ms']==r['sample_ms']: return
        t=snap['uptime_ms']/1000
        if self.samples and t<self.samples[-1]['time']: self.clear()
        self.samples.append(dict(time=t,sample_ms=r['sample_ms'],PB11=r['temperatures'][0]/10,
            PB10=r['temperatures'][1]/10,PB12=r['temperatures'][2]/10,PA5=r['temperatures'][3]/10,
            目标=r['heat_target']/10,加热=r['heat_output']/10,风机=r['fan_output'],
            积分=r['heat_integral']/10000,加热误差=r['heat_error']/10,风机误差=r['fan_error']/10))
        self.update()

    def clear(self):
        self.samples.clear()
        self.drag=None
        self.reset_view()

    def zoom(self,factor):
        self.span=max(5,min(86400,self.span*factor))
        self.update()

    def zoom_y(self,factor):
        low,high=self.y_ranges.get(self.group,self.last_range)
        center=(low+high)/2; span=max(.1,min(10000,(high-low)*factor))
        self.y_ranges[self.group]=(center-span/2,center+span/2)
        self.update()

    def reset_view(self):
        self.span,self.offset=120.,0.
        self.y_ranges.clear()
        self.update()

    def wheelEvent(self,event):
        fn=self.zoom_y if event.modifiers() & Qt.KeyboardModifier.ControlModifier else self.zoom
        fn(.75 if event.angleDelta().y()>0 else 4/3)

    def mousePressEvent(self,event):
        if event.button()==Qt.MouseButton.LeftButton: self.drag=event.position()

    def mouseMoveEvent(self,event):
        if self.drag is not None:
            x=event.position().x()
            maximum=max(0,self.samples[-1]['time']-self.samples[0]['time']) if self.samples else 0
            self.offset=max(0,min(maximum,self.offset+(x-self.drag.x())*self.span/max(1,self.width()-250)))
            low,high=self.y_ranges.get(self.group,self.last_range)
            dy=(event.position().y()-self.drag.y())*(high-low)/max(1,self.height()-78)
            self.y_ranges[self.group]=(low+dy,high+dy)
            self.drag=event.position()
            self.update()

    def mouseReleaseEvent(self,event): self.drag=None
    def mouseDoubleClickEvent(self,event): self.reset_view()

    def paintEvent(self,event):
        p=QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.fillRect(self.rect(),QColor('#101b29'))
        rect=QRectF(58,38,max(1,self.width()-250),max(1,self.height()-78))
        chosen=[(n,c) for n,c in SERIES[self.group] if n not in self.hidden]
        right=self.samples[-1]['time']-self.offset if self.samples else self.span
        left=right-self.span
        samples=self.samples.visible(left,right)
        values=[s[n] for s in samples for n,c in chosen]
        low,high=(min(values),max(values)) if values else (0,100)
        pad=max(1,(high-low)*.12)
        low,high=low-pad,high+pad
        low,high=self.y_ranges.get(self.group,(low,high))
        self.last_range=(low,high)
        for i in range(5):
            y=rect.top()+rect.height()*i/4
            p.setPen(QColor('#29384a'))
            p.drawLine(QPointF(rect.left(),y),QPointF(rect.right(),y))
            p.setPen(QColor('#a8bacb'))
            p.drawText(QRectF(0,y-10,50,20),Qt.AlignmentFlag.AlignRight,f'{high-(high-low)*i/4:.1f}')
            x=rect.left()+rect.width()*i/4
            p.drawText(QRectF(x-32,rect.bottom()+8,80,20),Qt.AlignmentFlag.AlignLeft,f'{left+self.span*i/4:.0f}s')
        p.setPen(QColor('#a8bacb'))
        p.drawText(QPointF(rect.left(),22),'综合曲线 · 温度/误差 °C · 输出 %（同数值刻度）')
        annotations=[]
        for name,color in chosen:
            path=QPainterPath()
            for i,s in enumerate(samples):
                pt=QPointF(rect.left()+(s['time']-left)/self.span*rect.width(),
                           rect.bottom()-(s[name]-low)/(high-low)*rect.height())
                if i==0: path.moveTo(pt)
                else: path.lineTo(pt)
            p.setPen(QPen(QColor(color),1.8))
            p.setBrush(Qt.BrushStyle.NoBrush)
            p.save(); p.setClipRect(rect); p.drawPath(path); p.restore()
            if self.samples and left<=self.samples[-1]['time']<=right:
                latest=self.samples[-1]
                x=rect.left()+(latest['time']-left)/self.span*rect.width()
                y=rect.bottom()-(latest[name]-low)/(high-low)*rect.height()
                if rect.top()<=y<=rect.bottom():
                    p.setBrush(QColor(color)); p.drawEllipse(QPointF(x,y),3.5,3.5)
                    annotations.append((y,name,color,x,latest[name]))
        # Place endpoint values in a gutter; keep nearby values readable.
        annotations.sort()
        positions=[]
        for y,*_ in annotations:
            positions.append(max(y,positions[-1]+20 if positions else rect.top()+10))
        if positions and positions[-1]>rect.bottom()-5:
            positions[-1]=rect.bottom()-5
            for i in range(len(positions)-2,-1,-1):
                positions[i]=min(positions[i],positions[i+1]-20)
        for (y,name,color,x,value),label_y in zip(annotations,positions):
            p.setPen(QPen(QColor(color),1))
            p.drawLine(QPointF(x+4,y),QPointF(rect.right()+10,label_y))
            p.drawText(QPointF(rect.right()+14,label_y+4),
                       f'{LABELS.get(name,name)} {value:.1f}{UNITS[name]}')
        if not samples:
            p.setPen(QColor('#a8bacb'))
            p.drawText(rect,Qt.AlignmentFlag.AlignCenter,'等待设备数据')
        p.end()
