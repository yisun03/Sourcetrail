#include "component/controller/helper/GraphPostprocessor.h"

#include "component/view/GraphViewStyle.h"

unsigned int GraphPostprocessor::s_cellWidth = GraphViewStyle::s_gridCellSize;
unsigned int GraphPostprocessor::s_cellHeight = GraphViewStyle::s_gridCellSize;
unsigned int GraphPostprocessor::s_cellPadding = GraphViewStyle::s_gridCellPadding;

void GraphPostprocessor::doPostprocessing(std::vector<DummyNode>& nodes)
{
	unsigned int atomarGridWidth = s_cellWidth;
	unsigned int atomarGridHeight = s_cellHeight;

	if (nodes.size() < 2)
	{
		LOG_INFO_STREAM(<< "Skipping postprocessing, need at least 2 nodes but got " << nodes.size());
		return;
	}

	// determine center of mass (CoD) which is used to get outliers closer to the rest of the graph
	int divisorWidth = 999999;
	int divisorHeight = 999999;
	int maxNodeWidth = 0;
	int maxNodeHeight = 0;
	Vec2i centerOfMass(0, 0);
	float totalMass = 0.0f;

	for (const DummyNode& node : nodes)
	{
		if (node.size.x < divisorWidth)
		{
			divisorWidth = node.size.x;
		}

		if (node.size.y < divisorHeight)
		{
			divisorHeight = node.size.y;
		}

		if (node.size.x > maxNodeWidth)
		{
			maxNodeWidth = node.size.x;
		}
		else if (node.size.y > maxNodeHeight)
		{
			maxNodeHeight = node.size.y;
		}

		float nodeMass = node.size.x * node.size.y;
		centerOfMass += node.position * nodeMass;
		totalMass += nodeMass;
	}

	centerOfMass /= totalMass;

	divisorWidth = (int)atomarGridWidth + s_cellPadding; //std::min(divisor, (int)atomarGridSize);
	divisorHeight = (int)atomarGridHeight + s_cellPadding;

	resolveOutliers(nodes, centerOfMass);

	// the nodes will be aligned everytime they move during post-processing
	// align all nodes once here so that nodes that won't be moved again are aligned
	for (DummyNode& node : nodes)
	{
		alignNodeOnRaster(node);
	}

	MatrixDynamicBase<unsigned int> heatMap = buildHeatMap(nodes, divisorWidth, divisorHeight, maxNodeWidth, maxNodeHeight);

	resolveOverlap(nodes, heatMap, divisorWidth, divisorHeight);
}

void GraphPostprocessor::alignNodeOnRaster(DummyNode& node)
{
	node.position = alignOnRaster(node.position);
}

Vec2i GraphPostprocessor::alignOnRaster(Vec2i position)
{
	int rasterPosDivisor = s_cellWidth + s_cellPadding;

	if (position.x % rasterPosDivisor != 0)
	{
		int t = position.x / rasterPosDivisor;
		int r = position.x % rasterPosDivisor;

		if (std::abs(r) > rasterPosDivisor/2)
		{
			if (t != 0)
			{
				t += (t / std::abs(t));
			}
			else if (r != 0)
			{
				t += (r / std::abs(r));
			}
		}

		position.x = t * rasterPosDivisor;
	}

	if (position.y % rasterPosDivisor != 0)
	{
		int t = position.y / rasterPosDivisor;
		int r = position.y % rasterPosDivisor;

		if (std::abs(r) > rasterPosDivisor/2)
		{
			if(t != 0)
			{
				t += (t / std::abs(t));
			}
			else if(r != 0)
			{
				t += (r / std::abs(r));
			}
		}

		position.y = t * rasterPosDivisor;
	}

	return position;
}

void GraphPostprocessor::resolveOutliers(std::vector<DummyNode>& nodes, const Vec2i& centerPoint)
{
	float maxDist = 0.0f;
	for (const DummyNode& node : nodes)
	{
		Vec2i toCenterOfMass = centerPoint - node.position;
		if (toCenterOfMass.getLength() > maxDist)
		{
			maxDist = toCenterOfMass.getLength();
		}
	}

	if (maxDist == 0.0f)
	{
		return;
	}

	for (DummyNode& node : nodes)
	{
		Vec2i toCenterOfMass = centerPoint - node.position;
		float dist = toCenterOfMass.getLength();

		// causes far away nodes to be effected stronger than nodes that are already close to the center
		float distFactor = std::sqrt(dist / maxDist);
		node.position += toCenterOfMass * distFactor;
	}
}

MatrixDynamicBase<unsigned int> GraphPostprocessor::buildHeatMap(
	const std::vector<DummyNode>& nodes, const int atomarNodeWidth, const int atomarNodeHeight, const int maxNodeWidth, const int maxNodeHeight)
{
	int heatMapWidth = (maxNodeWidth * nodes.size() / atomarNodeWidth) * 5; // theoretically the nodes could horizontally or vertically far from the center, therefore '*5' (it's kinda arbitrary, generally *2 should suffice, I use *5 to prevent problems in extrem cases)
	int heatMapHeight = (maxNodeHeight * nodes.size() / atomarNodeHeight) * 5;

	MatrixDynamicBase<unsigned int> heatMap(heatMapWidth, heatMapHeight);

	for (const DummyNode& node : nodes)
	{
		int left = node.position.x / atomarNodeWidth + heatMapWidth/2;
		int up = node.position.y / atomarNodeHeight + heatMapHeight/2;
		Vec2i size = calculateRasterNodeSize(node);
		int width = size.x;
		int height = size.y;

		if (left + width > heatMapWidth || left < 0)
		{
			continue;
		}

		if (up + height > heatMapHeight || up < 0)
		{
			continue;
		}

		for (int i = 0; i < width; i++)
		{
			for (int j = 0; j < height; j++)
			{
				unsigned int x = left + i;
				unsigned int y = up + j;
				unsigned int value = heatMap.getValue(x, y);

				heatMap.setValue(x, y, value+1);
			}
		}
	}

	return heatMap;
}

void GraphPostprocessor::resolveOverlap(
	std::vector<DummyNode>& nodes, MatrixDynamicBase<unsigned int>& heatMap, const int divisorWidth, const int divisorHeight)
{
	int heatMapWidth = heatMap.getColumnsCount();
	int heatMapHeight = heatMap.getRowsCount();

	bool overlap = true;
	int iterationCount = 0;
	int maxIterations = 15;

	while (overlap && iterationCount < maxIterations)
	{
		LOG_INFO_STREAM(<< iterationCount);

		overlap = false;
		iterationCount++;

		for (DummyNode& node : nodes)
		{
			Vec2i nodePos(0, 0);
			nodePos.x = node.position.x / divisorWidth + heatMapWidth/2;
			nodePos.y = node.position.y / divisorHeight + heatMapHeight/2;
			Vec2i nodeSize = calculateRasterNodeSize(node);

			if (nodePos.x + nodeSize.x > heatMapWidth || nodePos.x < 0)
			{
				LOG_WARNING("Leaving heatmap area in x");
				continue;
			}

			if (nodePos.y + nodeSize.y > heatMapHeight || nodePos.y < 0)
			{
				LOG_WARNING("Leaving heatmap area in y");
				continue;
			}

			Vec2f grad(0.0f, 0.0f);
			if (getHeatmapGradient(grad, heatMap, nodePos, nodeSize))
			{
				overlap = true;
			}

			// handle overlap with no gradient
			// e.g. when a node lies completely on top of another
			if (grad.getLengthSquared() <= 0.000001f && overlap)
			{
				grad = node.position;
				grad.normalize();

				// catch special case of node being at position 0/0
				if (grad.getLengthSquared() <= 0.000001f)
				{
					grad.y = 1.0f;
				}

				grad *= -1.0f;
			}

			// remove node temporarily from heat map, it will be re-added at the new position later on
			modifyHeatmapArea(heatMap, nodePos, nodeSize, -1);

			// move node to new position
			int xOffset = grad.x * divisorWidth;
			int yOffset = grad.y * divisorHeight;

			int maxXOffset = 2*divisorWidth;
			int maxYOffset = 2*divisorHeight;
			
			// prevent the graph from "exploding" again...
			if (xOffset > maxXOffset)
			{
				xOffset = maxXOffset;
			}
			else if (xOffset < -maxXOffset)
			{
				xOffset = -maxXOffset;
			}

			if (yOffset > maxYOffset)
			{
				yOffset = maxYOffset;
			}
			else if (yOffset < -maxYOffset)
			{
				yOffset = -maxYOffset;
			}

			node.position += Vec2i(xOffset, yOffset);

			alignNodeOnRaster(node);

			// re-add node to heat map at new position
			nodePos.x = node.position.x / divisorWidth + heatMapWidth/2;
			nodePos.y = node.position.y / divisorHeight + heatMapHeight/2;

			modifyHeatmapArea(heatMap, nodePos, nodeSize, 1);

			if (getHeatmapGradient(grad, heatMap, nodePos, nodeSize))
			{
				overlap = true;
			}
		}
	}
}

void GraphPostprocessor::modifyHeatmapArea(
	MatrixDynamicBase<unsigned int>& heatMap, const Vec2i& leftUpperCorner, const Vec2i& size, const int modifier
){
	bool wentOutOfRange = false;

	for (int i = 0; i < size.x; i++)
	{
		for (int j = 0; j < size.y; j++)
		{
			int x = leftUpperCorner.x + i;
			int y = leftUpperCorner.y + j;

			if (x < 0 || x > static_cast<int>(heatMap.getColumnsCount()-1))
			{
				wentOutOfRange = true;
				continue;
			}
			if (y < 0 || y > static_cast<int>(heatMap.getRowsCount()-1))
			{
				wentOutOfRange = true;
				continue;
			}

			unsigned int value = heatMap.getValue(x, y);
			heatMap.setValue(x, y, value+modifier);

			if (wentOutOfRange == true)
			{
				LOG_WARNING("Left matrix range while trying to modify values.");
			}
		}
	}
}

bool GraphPostprocessor::getHeatmapGradient(
	Vec2f& outGradient, const MatrixDynamicBase<unsigned int>& heatMap, const Vec2i& leftUpperCorner, const Vec2i& size
){
	bool overlap = false;

	for (int i = 0; i < size.x; i++)
	{
		for (int j = 0; j < size.y; j++)
		{
			int x = leftUpperCorner.x + i;
			int y = leftUpperCorner.y + j;

			// weight factors that emphasize gradients near the nodes center
			int hMagFactor = std::max(1, (int)(size.x * 0.5 - std::abs(i + 1 - size.x * 0.5)));
			int vMagFactor = std::max(1, (int)(size.y * 0.5 - std::abs(j + 1 - size.y * 0.5)));

			// if x and y lie directly at the border not all 4 neighbours can be checked
			if (x < 1 || x > static_cast<int>(heatMap.getColumnsCount() - 2))
			{
				continue;
			}
			if (y < 1 || y > static_cast<int>(heatMap.getRowsCount() - 2))
			{
				continue;
			}

			float val = heatMap.getValue(x, y);

			float xP1 = heatMap.getValue(x + 1, y) * hMagFactor;
			float xM1 = heatMap.getValue(x - 1, y) * hMagFactor;
			float yP1 = heatMap.getValue(x, y + 1) * vMagFactor;
			float yM1 = heatMap.getValue(x, y - 1) * vMagFactor;

			xP1 = std::sqrt(xP1);
			xM1 = std::sqrt(xM1);
			yP1 = std::sqrt(yP1);
			yM1 = std::sqrt(yM1);

			float xOffset = (xM1 - val) + (val - xP1);
			float yOffset = (yM1 - val) + (val - yP1);

			outGradient += Vec2f(xOffset, yOffset);

			if (val > 1)
			{
				overlap = true;
			}
		}
	}

	return overlap;
}

Vec2f GraphPostprocessor::heatMapRayCast(
	const MatrixDynamicBase<unsigned int>& heatMap, const Vec2f& startPosition, const Vec2f& direction, unsigned int minValue
){
	float xOffset = 0.0f;
	float yOffset = 0.0f;

	if (std::abs(direction.x) > 0.0000000001f)
	{
		xOffset = direction.x / std::abs(direction.x);
	}
	if (std::abs(direction.y) > 0.0000000001f)
	{
		yOffset = direction.y / std::abs(direction.y);
	}

	if (startPosition.x < 1 || startPosition.x > static_cast<int>(heatMap.getColumnsCount() - 2))
	{
		return Vec2f(0.0f, 0.0f);
	}
	if (startPosition.y < 1 || startPosition.y > static_cast<int>(heatMap.getRowsCount() - 2))
	{
		return Vec2f(0.0f, 0.0f);
	}

	Vec2f length(0.0f, 0.0f);

	bool hit = false;

	float posX = startPosition.x + xOffset;
	float posY = startPosition.y + yOffset;

	do
	{
		if (heatMap.getValue(posX, posY) >= minValue)
		{
			hit = true;
			length.x = length.x + xOffset;
			length.y = length.y + yOffset;

			posX += xOffset;
			posY += yOffset;
		}
		else
		{
			hit = false;
		}
	}
	while (hit);

	return length;
}

Vec2i GraphPostprocessor::calculateRasterNodeSize(const DummyNode& node)
{
	Vec2i size = node.size;
	Vec2i rasterSize(0, 0);

	while (size.x > 0)
	{
		size.x = size.x - s_cellWidth;
		if(size.x > 0)
		{
			size.x = size.x - s_cellPadding;
		}

		rasterSize.x = rasterSize.x + 1;
	}

	while (size.y > 0)
	{
		size.y = size.y - s_cellHeight;
		if(size.y > 0)
		{
			size.y = size.y - s_cellPadding;
		}

		rasterSize.y = rasterSize.y + 1;
	}

	return rasterSize;
}
