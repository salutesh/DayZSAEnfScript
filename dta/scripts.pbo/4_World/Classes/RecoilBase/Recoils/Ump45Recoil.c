class Ump45Recoil: RecoilBase
{
	override void Init()
	{
		vector point_1;
		vector point_2;
		vector point_3;
		vector point_4;
		point_1[0] = Math.RandomFloatInclusive(0.5,1.25); point_1[1] = 1.1; point_1[2] = 0;
		point_2[0] = Math.RandomFloatInclusive(-1,1); point_2[1] = 1.3; point_2[2] = 0;
		point_3[0] = Math.RandomFloatInclusive(-0.75,-1.75); point_3[1] = 1.1; point_3[2] = 0;
		point_4[0] = Math.RandomFloatInclusive(-0.5,-0.1); point_4[1] = 0.75; point_4[2] = 0;
		m_HandsCurvePoints.Insert(point_1);//forms a 2 dimensional spline(z is ignored)
		m_HandsCurvePoints.Insert(point_2);
		m_HandsCurvePoints.Insert(point_3);
		m_HandsCurvePoints.Insert(point_4);
		m_HandsCurvePoints.Insert("0 0 0");
		m_HandsOffsetRelativeTime = 1;
		
		m_MouseOffsetRangeMin = 50;//in degrees min
		m_MouseOffsetRangeMax = 120;//in degrees max
		m_MouseOffsetDistance = 1.05;//how far should the mouse travel
		m_MouseOffsetRelativeTime = 0.5;//[0..1] a time it takes to move the mouse the required distance relative to the reload time of the weapon(firing mode)
	}
}